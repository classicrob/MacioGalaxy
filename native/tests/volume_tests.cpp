// The master volume setting (petari/volume.hpp): snapping, gain, sound.txt.
#include <petari/volume.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace Volume = PetariNative::Volume;

namespace {

int checks = 0;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void testSnapAndGain() {
    Volume::resetForTesting();
    check(Volume::percent() == 100 && Volume::gain() == 1.0f, "default is full volume");
    static_assert(Volume::snap(-7) == 0 && Volume::snap(2) == 0 && Volume::snap(3) == 5 && Volume::snap(83) == 85 &&
                  Volume::snap(130) == 100);
    Volume::setPercent(52);
    check(Volume::percent() == 50 && std::fabs(Volume::gain() - 0.25f) < 1e-6f, "52 snaps to 50, gain 0.25");
    Volume::setPercent(0);
    check(Volume::gain() == 0.0f, "0 is silent");
}

void testParse() {
    Volume::resetForTesting();
    std::string error;
    check(Volume::parse("# comment\nOther=1\n  Volume = 35  \n", &error) && Volume::percent() == 35,
          "Volume is read, comments and unknown lines are ignored");
    check(Volume::parse("", &error) && Volume::percent() == 35, "no Volume line keeps the value");
    check(!Volume::parse("Volume=loud\n", &error) && Volume::percent() == 35 && error.find("line 1") != std::string::npos,
          "a bad value is refused with its line, nothing changes");
    check(!Volume::parse("Volume=101\n", &error) && !Volume::parse("Volume=-5\n", &error) && Volume::percent() == 35,
          "values outside 0..100 are refused");
    check(Volume::parse(Volume::serialize(), &error) && Volume::percent() == 35, "serialize round-trips");
}

void testFile() {
    Volume::resetForTesting();
    char pattern[] = "/tmp/petari-volume-XXXXXX";
    const char* dir = mkdtemp(pattern);
    check(dir != nullptr, "temporary directory");
    const std::filesystem::path file = std::filesystem::path(dir) / "sound.txt";
    std::string error;
    Volume::setPercent(40);
    check(Volume::save(&error) && !std::filesystem::exists(file), "save before load writes nothing");
    check(Volume::load(file, &error) && Volume::percent() == 40, "a missing file keeps the value");
    Volume::setPercent(65);
    check(Volume::save(&error) && read(file).find("Volume=65\n") != std::string::npos, "save writes the value");
    Volume::setPercent(10);
    check(Volume::load(file, &error) && Volume::percent() == 65, "load reads it back");
    std::ofstream(file) << "Volume=x\n";
    check(!Volume::load(file, &error) && error.find(file.string()) == 0 && Volume::percent() == 65,
          "a damaged file is reported with its path and changes nothing");
    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    testSnapAndGain();
    testParse();
    testFile();
    std::printf("native volume tests passed (%d checks)\n", checks);
    return 0;
}
