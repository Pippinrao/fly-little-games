#import <UIKit/UIKit.h>

#include "portability_smoke.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

NSString* ns_string(const std::string& value)
{
    return [[NSString alloc] initWithBytes:value.data()
                                    length:value.size()
                                  encoding:NSUTF8StringEncoding];
}

flynes::ios::PortabilitySmokeResult app_failure(const char* stage)
{
    flynes::ios::PortabilitySmokeResult result;
    result.report = std::string("FLYNES_IOS_SMOKE_FAIL stage=") + stage + " code=0";
    return result;
}

bool write_result_json(NSURL* documents_url, const flynes::ios::PortabilitySmokeResult& result)
{
    NSString* const status = result.exit_code == 0 ? @"PASS" : @"FAIL";
    NSString* const report = ns_string(result.report);
    NSString* const full_hash = ns_string(result.full_file_sha256);
    NSString* const core_hash = ns_string(result.core_cartridge_sha1);
    if (report == nil || full_hash == nil || core_hash == nil)
        return false;

    NSDictionary* const object = @{
        @"schema_version" : @1,
        @"status" : status,
        @"exit_code" : @(result.exit_code),
        @"frames_run" : @(result.frames_run),
        @"audio_samples" : @(result.audio_samples),
        @"audio_samples_changed" : @(result.audio_samples_changed),
        @"state_bytes" : @(result.state_bytes),
        @"input_generation" : @(result.input_generation),
        @"input_monotonic_ns" : @(result.input_monotonic_ns),
        @"input_pad0" : @(result.input_pad_bits[0]),
        @"input_pad1" : @(result.input_pad_bits[1]),
        @"input_pad2" : @(result.input_pad_bits[2]),
        @"input_pad3" : @(result.input_pad_bits[3]),
        @"video_sequence" : @(result.video_sequence),
        @"video_monotonic_ns" : @(result.video_monotonic_ns),
        @"non_black_pixels" : @(result.non_black_pixels),
        @"catalog_generation" : @(result.catalog_generation),
        @"catalog_count" : @(result.catalog_count),
        @"full_file_sha256" : full_hash,
        @"core_cartridge_sha1" : core_hash,
        @"report" : report,
    };

    NSError* serialization_error = nil;
    NSData* const json = [NSJSONSerialization dataWithJSONObject:object
                                                         options:NSJSONWritingSortedKeys
                                                           error:&serialization_error];
    if (json == nil || serialization_error != nil)
        return false;
    NSURL* const result_url =
        [documents_url URLByAppendingPathComponent:@"flynes-ios-stage1-result.json"];
    return [json writeToURL:result_url options:NSDataWritingAtomic error:nil];
}

} // namespace

@interface FlyNESPortabilityAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation FlyNESPortabilityAppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary<UIApplicationLaunchOptionsKey, id>*)launchOptions
{
    (void)application;
    (void)launchOptions;

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController* const controller = [[UIViewController alloc] init];
    controller.view.backgroundColor = UIColor.blackColor;
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];

    dispatch_async(dispatch_get_main_queue(), ^{
      NSFileManager* const file_manager = NSFileManager.defaultManager;
      NSURL* const documents_url =
          [file_manager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
      NSURL* const cache_url =
          [file_manager URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject;
      NSString* const rom_path = [NSBundle.mainBundle pathForResource:@"from_below" ofType:@"nes"];
      NSString* const license_path =
          [NSBundle.mainBundle pathForResource:@"LICENSE-from-below" ofType:@"txt"];

      flynes::ios::PortabilitySmokeResult result;
      if (documents_url == nil || cache_url == nil)
      {
          result = app_failure("sandbox_roots");
      }
      else if (rom_path == nil || license_path == nil)
      {
          result = app_failure("bundle_resources");
      }
      else
      {
          NSError* read_error = nil;
          NSData* const rom = [NSData dataWithContentsOfFile:rom_path
                                                    options:NSDataReadingMappedIfSafe
                                                      error:&read_error];
          NSData* const license = [NSData dataWithContentsOfFile:license_path];
          if (rom == nil || rom.length == 0u || read_error != nil ||
              license == nil || license.length == 0u)
          {
              result = app_failure("read_bundle_resources");
          }
          else
          {
              const char* const data_root_utf8 = documents_url.path.UTF8String;
              const char* const cache_root_utf8 = cache_url.path.UTF8String;
              if (data_root_utf8 == nullptr || cache_root_utf8 == nullptr)
              {
                  result = app_failure("sandbox_root_utf8");
              }
              else
              {
                  const std::string data_root(data_root_utf8);
                  const std::string cache_root(cache_root_utf8);
                  result = flynes::ios::run_portability_smoke({
                      data_root,
                      cache_root,
                      static_cast<const std::uint8_t*>(rom.bytes),
                      static_cast<std::size_t>(rom.length),
                  });
              }
          }
      }

      if (documents_url == nil || !write_result_json(documents_url, result))
      {
          std::fprintf(stderr, "FLYNES_IOS_SMOKE_FAIL stage=write_result_json code=0\n");
          std::fflush(stderr);
          std::exit(2);
      }

      std::fprintf(result.exit_code == 0 ? stdout : stderr, "%s\n", result.report.c_str());
      std::fflush(stdout);
      std::fflush(stderr);
      std::exit(result.exit_code);
    });

    return YES;
}

@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(
            argc, argv, nil, NSStringFromClass(FlyNESPortabilityAppDelegate.class));
    }
}
