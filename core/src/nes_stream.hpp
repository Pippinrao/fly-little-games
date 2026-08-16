/*
 * core/src/nes_stream.hpp — 内存字节流适配器 (t2b §4)
 *
 * NestopiaUE 的所有 IO (Machine::Load / LoadState / SaveState / Fds::SetBIOS / Patch)
 * 都走 std::istream / std::ostream, 而 C ABI 面是「字节指针 + 长度」。
 * 本文件提供两类适配器把两者桥接起来:
 *
 *   MemIStream  — 只读输入流, 包住 [data, data+size)。
 *   MemOStream  — 只写输出流, 带容量上限检查 (save state 用)。
 *
 * 约定 (t2b §4.4/§4.6):
 *   - 适配器与流对象必须在调用 Machine::Load/SaveState 的同一个栈帧内活过全程;
 *   - 不抛自定义异常, 超容用「截断 + badbit」表达 (Nestopia 会把 bad_alloc 捕获为
 *     RESULT_ERR_OUT_OF_MEMORY, 把坏流写抛为 RESULT_ERR_CORRUPT_FILE);
 *   - 非阻塞、不得重入 nes_*。
 *
 * 注意: t2b §4.2 曾断言「顺序读、不需要 seek」, 但真实源码并非如此:
 *   - iNES/UNIF 加载 (NstCartridgeInes.cpp / NstCartridgeUnif.cpp /
 *     NstCartridgeRomset.cpp) 会调用 stream.Seek()/stream.Length()
 *     (即 seekg/tellg) → MemIStream 必须实现 seekoff/seekpos;
 *   - 状态存档器 (NstState.cpp Saver::End) 每个 chunk 结束后用
 *     stream.Seek(...) (即 ostream::seekp) 回填 chunk 长度, 然后再 Seek 回来
 *     → MemOStream 同样必须实现 seekoff/seekpos, 否则 Machine::SaveState
 *     在第一个 End() 处抛 RESULT_ERR_CORRUPT_FILE (NstStream.cpp
 *     Stream::Out::Seek), 使 nes_save_state 恒失败。
 *
 * 命名: 不用 namespace nes —— nes.h 的 C 句柄标签 `struct nes` 与之冲突。
 */
#ifndef NES_STREAM_HPP
#define NES_STREAM_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <streambuf>
#include <vector>

namespace nes_stream
{
	// ------------------------------------------------------------------
	// 只读内存输入流
	// ------------------------------------------------------------------
	class MemIStream
	{
	public:
		MemIStream(const uint8_t* data, size_t size);
		std::istream& stream() { return in_; }

	private:
		struct Buf : std::streambuf
		{
			Buf(const uint8_t* data, size_t size);
			int_type underflow() override;
			pos_type seekoff(off_type off, std::ios_base::seekdir dir,
			                 std::ios_base::openmode which) override;
			pos_type seekpos(pos_type pos, std::ios_base::openmode which) override;
		};

		Buf buf_;
		std::istream in_;
	};

	// ------------------------------------------------------------------
	// 只写内存输出流 (save state 等), 带容量上限:
	//   - 超容时置 overflowed 标志, xsputn 返回 0 / overflow 返回 eof,
	//     使 std::ostream 进入 badbit (Nestopia 的 Stream::Out::Write
	//     会因此抛 RESULT_ERR_CORRUPT_FILE, 壳层据此映射 NES_ERR_BUFFER_TOO_SMALL);
	//   - 内部以写游标 cur_ + 已写范围 end_ 建模, 实现 seekoff/seekpos
	//     (状态存档器在 Saver::End 回填 chunk 长度时需要 seekp);
	//   - written(): 缓冲内有效字节数 (end_, 超容时即已写入前缀);
	//   - needed(): 真实需求下界 = end_ + 首次失败写「本会扩展流」的字节数
	//     (Nestopia 在首次写失败处立即中止, 剩余大小未知, 故只是下界;
	//     成功完整写出后 == written() == 最终流长度)。
	// ------------------------------------------------------------------
	class MemOStream
	{
	public:
		MemOStream(uint8_t* buf, size_t cap, size_t* written, size_t* needed);
		std::ostream& stream() { return out_; }
		bool overflowed() const { return overflowed_; }
		size_t written() const { return buf_.end_; }
		size_t needed() const { return buf_.needed_total_; }

	private:
		struct Buf : std::streambuf
		{
			Buf(uint8_t* buf, size_t cap, size_t* written, size_t* needed, bool* overflowed);
			int_type overflow(int_type c) override;
			std::streamsize xsputn(const char* s, std::streamsize n) override;
			pos_type seekoff(off_type off, std::ios_base::seekdir dir,
			                 std::ios_base::openmode which) override;
			pos_type seekpos(pos_type pos, std::ios_base::openmode which) override;
			int sync() override;

			uint8_t* buf_;
			size_t cap_;
			size_t cur_;            // 当前写游标 (seekp 的目标)
			size_t end_;            // 已写最大范围 = 缓冲内有效字节数 (成功时即最终流长度)
			size_t needed_total_;   // 真实需求下界 (end_ + 首次失败写超出 end_ 的字节)
			size_t* written_;       // 镜像 end_
			size_t* needed_;        // 镜像 needed_total_
			bool* overflowed_;
		};

		Buf buf_;
		std::ostream out_;
		bool overflowed_;
	};

	// ------------------------------------------------------------------
	// 可增长只写输出流 (内部工具, 无容量上限):
	//   - 后端为 std::vector<uint8_t>, 写满自动扩容 —— 用于 nes_save_state
	//     先「全量产出裸 NST 字节」再套 FLYNST1 安全包装头 (S1-2);
	//   - 与 MemOStream 一样必须实现 seekoff/seekpos: 状态存档器在
	//     NstState.cpp Saver::End 用 stream.Seek(...) 回填 chunk 长度,
	//     没有 seek 的话首个 End() 就抛 RESULT_ERR_CORRUPT_FILE (同 957e882);
	//   - 游标模型与 MemOStream 相同: cur_ = 写游标 (seekp 目标),
	//     data_.size() = 高水位 (完整写出后即最终流长度); 回填式写入
	//     落在 [cur_, cur_+n) 原地覆盖, 不会截断已有字节。
	// ------------------------------------------------------------------
	class GrowableOStream
	{
	public:
		GrowableOStream();
		std::ostream& stream() { return out_; }
		const std::vector<uint8_t>& data() const { return data_; }

	private:
		struct Buf : std::streambuf
		{
			Buf(std::vector<uint8_t>* data);
			int_type overflow(int_type c) override;
			std::streamsize xsputn(const char* s, std::streamsize n) override;
			pos_type seekoff(off_type off, std::ios_base::seekdir dir,
			                 std::ios_base::openmode which) override;
			pos_type seekpos(pos_type pos, std::ios_base::openmode which) override;
			int sync() override;

			std::vector<uint8_t>* data_;
			size_t cur_;   // 当前写游标 (seekp 的目标)
		};

		std::vector<uint8_t> data_;
		Buf buf_;
		std::ostream out_;
	};
}

#endif /* NES_STREAM_HPP */