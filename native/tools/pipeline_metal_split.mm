// Splits a GX pipeline's Metal compile into VS newLibraryWithSource, FS newLibraryWithSource and PSO creation
// (native/tools/PIPELINE_SEEDING.md). Inputs: MSL pairs extracted from pipeline_compile_bench --dump.
// Build: clang++ -std=c++20 -O1 -fobjc-arc native/tools/pipeline_metal_split.mm -framework Metal -framework Foundation
// Usage: pipeline_metal_split NAMESPACE vs.metal fs.metal [vs fs ...]   (delete the printed NAMESPACE dir afterwards)
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <string>
static double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char** argv) {
    using SetSuffix = int (*)(const char*);
    auto setSuffix = reinterpret_cast<SetSuffix>(dlsym(RTLD_DEFAULT, "_set_user_dir_suffix"));
    setSuffix(argv[1]);
    char dir[1024];
    confstr(_CS_DARWIN_USER_CACHE_DIR, dir, sizeof dir);
    printf("NAMESPACE %s\n", dir);
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        double tv = 0, tf = 0, tp = 0;
        int n = 0;
        for (int i = 2; i + 1 < argc; i += 2) {
            NSString* vs = [NSString stringWithContentsOfFile:@(argv[i]) encoding:NSUTF8StringEncoding error:nil];
            NSString* fs = [NSString stringWithContentsOfFile:@(argv[i + 1]) encoding:NSUTF8StringEncoding error:nil];
            MTLCompileOptions* opt = [MTLCompileOptions new];
            NSError* err = nil;
            double a = now();
            id<MTLLibrary> lv = [dev newLibraryWithSource:vs options:opt error:&err];
            double b = now();
            id<MTLLibrary> lf = [dev newLibraryWithSource:fs options:opt error:&err];
            double c = now();
            if (!lv || !lf) { fprintf(stderr, "lib fail %s\n", err.localizedDescription.UTF8String); return 1; }
            MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
            d.vertexFunction = [lv newFunctionWithName:@"dawn_entry_point"];
            d.fragmentFunction = [lf newFunctionWithName:@"dawn_entry_point"];
            d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
            d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:d error:&err];
            double e = now();
            if (!ps) { fprintf(stderr, "pso fail %s\n", err.localizedDescription.UTF8String); }
            printf("%s vs_lib=%.1f fs_lib=%.1f pso=%.1f\n", argv[i], b - a, c - b, e - c);
            tv += b - a; tf += c - b; tp += e - c; ++n;
        }
        printf("n=%d vs_lib=%.0f fs_lib=%.0f pso=%.0f (ms totals)\n", n, tv, tf, tp);
    }
}
