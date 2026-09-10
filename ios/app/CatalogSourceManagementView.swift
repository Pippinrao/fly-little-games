import SwiftUI
import UniformTypeIdentifiers

struct CatalogSource: Identifiable {
    let id: String
    let name: String
    let directory: Bool
    let error: String
}

/// File-provider work is serialized off the main thread; UI publishes snapshots.
final class CatalogSourceModel: ObservableObject {
    static let shared = CatalogSourceModel()
    @Published var sources: [CatalogSource] = []
    @Published var busy = false
    @Published var error: String?
    @Published var generation = 0
    private let queue = DispatchQueue(label: "com.flynes.catalog.sources", qos: .userInitiated)
    private var initialized = false

    func initialize() {
        guard !initialized else { return }
        initialized = true
        perform { try CatalogSourceService.sharedInstance().prepareBuiltin() }
    }

    func perform(_ operation: @escaping () throws -> Void) {
        guard !busy else { return }
        busy = true
        error = nil
        queue.async {
            var failure: String?
            do { try operation() } catch { failure = error.localizedDescription }
            let rows = CatalogSourceService.sharedInstance().sources().compactMap { row -> CatalogSource? in
                guard let uuid = row["uuid"] as? String, let name = row["name"] as? String else { return nil }
                return CatalogSource(id: uuid, name: name,
                                     directory: (row["scope"] as? NSNumber)?.uint32Value == 2,
                                     error: row["error"] as? String ?? "")
            }
            DispatchQueue.main.async {
                self.sources = rows
                self.error = failure
                self.busy = false
                self.generation += 1
            }
        }
    }

    func prepareROM(_ canonicalID: String, completion: @escaping (Result<Data, Error>) -> Void) {
        queue.async {
            let result = Result { try CatalogSourceService.sharedInstance().romData(canonicalID: canonicalID) }
            DispatchQueue.main.async { completion(result) }
        }
    }
}

struct CatalogSourceManagementView: View {
    @ObservedObject private var model = CatalogSourceModel.shared
    @State private var showImporter = false
    @State private var pickDirectory = false
    @State private var reauthorizeSource: CatalogSource?

    var onClose: (() -> Void)? = nil

    var body: some View {
        GeometryReader { geometry in
            HStack(spacing: 8) {
                VStack(alignment: .leading, spacing: 12) {
                    Text("library.sources").font(.title2.bold()).accessibilityAddTraits(.isHeader)
                    ScrollView {
                        Text("library.source.formats").foregroundColor(.secondary)
                            .frame(maxWidth: .infinity, alignment: .leading)
                        Text("library.source.remove_hint").font(.footnote).foregroundColor(.secondary)
                            .padding(.top, 12)
                    }
                    Button { openPicker(directory: true) } label: {
                        Label("library.source.add_folder", systemImage: "folder.badge.plus")
                            .frame(maxWidth: .infinity, minHeight: 44)
                    }.buttonStyle(.borderedProminent).accessibilityIdentifier("add_source")
                    Button { openPicker(directory: false) } label: {
                        Label("library.source.add_files", systemImage: "doc.badge.plus")
                            .frame(maxWidth: .infinity, minHeight: 44)
                    }.buttonStyle(.bordered).accessibilityIdentifier("add_source_files")
                }
                .padding(20)
                .frame(width: max(0, (geometry.size.width - 8) * 0.34))
                .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 20))
                VStack(alignment: .leading, spacing: 0) {
                    HStack {
                        Text("library.source.imported").font(.headline).accessibilityAddTraits(.isHeader)
                        Spacer()
                        if let close = onClose {
                            Button(action: close) { Image(systemName: "xmark").frame(width: 48, height: 48) }
                                .accessibilityLabel(Text("common.done"))
                                .accessibilityIdentifier("close_sources")
                        }
                    }.frame(minHeight: 48)
                    if model.busy { HStack { ProgressView(); Text("library.source.working") } }
                    ScrollView {
                        LazyVStack(alignment: .leading, spacing: 12) {
                            if let error = model.error { Text(error).foregroundColor(.red).padding(.vertical, 8) }
                            if model.sources.isEmpty { Text("library.source.none").foregroundColor(.secondary) }
                            ForEach(model.sources) { source in
                                VStack(alignment: .leading, spacing: 10) {
                                    Label(source.name, systemImage: source.directory ? "folder" : "doc").font(.headline)
                                    if !source.error.isEmpty {
                                        Text(source.error).font(.footnote).foregroundColor(.red)
                                        Text("library.source.reauthorize_hint").font(.footnote).foregroundColor(.secondary)
                                    }
                                    HStack {
                                        Button("library.source.rescan") {
                                            model.perform { try CatalogSourceService.sharedInstance().rescan(uuid: source.id) }
                                        }
                                        Button("library.source.reauthorize") {
                                            reauthorizeSource = source; pickDirectory = source.directory; showImporter = true
                                        }
                                        Spacer()
                                        Button(role: .destructive) {
                                            model.perform { try CatalogSourceService.sharedInstance().remove(uuid: source.id) }
                                        } label: { Label("library.source.remove", systemImage: "trash") }
                                    }.buttonStyle(.borderless)
                                }.padding(12)
                                    .background(Color(uiColor: .secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 12))
                            }
                        }.frame(maxWidth: .infinity, alignment: .leading)
                    }.accessibilityIdentifier("source_list")
                }
            }
        }
        .disabled(model.busy)
        .onAppear { model.initialize() }
        .fileImporter(isPresented: $showImporter,
                      allowedContentTypes: pickDirectory ? [.folder] : [.data, .archive],
                      allowsMultipleSelection: !pickDirectory && reauthorizeSource == nil) { result in
            let replacing = reauthorizeSource
            let directory = pickDirectory
            reauthorizeSource = nil
            switch result {
            case .failure(let error): model.error = error.localizedDescription
            case .success(let urls):
                model.perform {
                    if let replacing = replacing, let url = urls.first {
                        try CatalogSourceService.sharedInstance().reauthorize(uuid: replacing.id, url: url)
                    } else {
                        // Each selected file is an independent source. Continue after a failed file.
                        var failures: [String] = []
                        for url in urls {
                            do { _ = try CatalogSourceService.sharedInstance().add(url: url, directory: directory) }
                            catch { failures.append(error.localizedDescription) }
                        }
                        if !failures.isEmpty {
                            throw NSError(domain: "com.flynes.sources", code: 1,
                                          userInfo: [NSLocalizedDescriptionKey: failures.joined(separator: "\n")])
                        }
                    }
                }
            }
        }
    }

    private func openPicker(directory: Bool) {
        reauthorizeSource = nil
        pickDirectory = directory
        showImporter = true
    }
}
