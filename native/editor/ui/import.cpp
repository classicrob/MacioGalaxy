#include "host.hpp"
#include "imgui.h"
#include "panels.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <mutex>
#include <petari/editor/import_assets.hpp>
#include <petari/editor/terrain/brush.hpp>
#include <petari/efb_dump_mark.hpp>
#include <petari/host_allocation.hpp>

namespace PetariEditor::Ui {
    namespace {
        namespace I = Imports;
        struct Result {
            std::shared_ptr< I::Scene > scene;
            std::shared_ptr< I::Asset > asset;
            std::string error;
        };
        std::mutex gDialogMutex;
        std::string gChosen;
        bool gChosenReady = false, gChooseCollision = false, gOpen = false, gDirty = false;
        std::string gPath, gError, gCollisionPath;
        std::shared_ptr< I::Scene > gScene, gCollision;
        std::shared_ptr< I::Asset > gAsset;
        I::Options gOptions;
        std::future< Result > gJob;
        enum class Job { Load, Collision, Prepare };
        Job gJobKind = Job::Load;
        float gYaw = .6f;
        void dialogDone(void*, const char* path) {
            std::lock_guard< std::mutex > lock(gDialogMutex);
            gChosen = path ? path : "";
            gChosenReady = true;
        }
        void choose(bool collision) {
            if (auto dialog = Host::fileDialog()) {
                gChooseCollision = collision;
                dialog(false, collision ? gCollisionPath.c_str() : gPath.c_str(), "3D model", "gltf;glb;obj", dialogDone, nullptr);
            } else {
                gError = "Drop a glTF, GLB or OBJ file onto the window, or enter its path below.";
            }
        }
        void loadPath(std::string path, bool collision) {
            if (gJob.valid()) {
                gError = "Wait for the current import to finish before choosing another file.";
                return;
            }
            if (collision) {
                gCollisionPath = path;
            } else {
                gPath = path;
                gScene.reset();
                gAsset.reset();
                gOptions = I::Options{};
            }
            gError.clear();
            gJobKind = collision ? Job::Collision : Job::Load;
            gJob = std::async(std::launch::async, [path] {
                PetariNative::HostAllocationScope host;
                Result result;
                auto scene = I::load(path, &result.error);
                if (scene) {
                    result.scene = std::make_shared< I::Scene >(std::move(*scene));
                }
                return result;
            });
        }
        void service() {
            std::string chosen;
            bool ready = false;
            {
                std::lock_guard< std::mutex > lock(gDialogMutex);
                ready = gChosenReady;
                gChosenReady = false;
                chosen = std::move(gChosen);
            }
            if (ready && !chosen.empty()) {
                loadPath(chosen, gChooseCollision);
            }
            if (gJob.valid() && gJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                auto result = gJob.get();
                gError = result.error;
                if (gJobKind == Job::Load && result.scene) {
                    gScene = std::move(result.scene);
                    gOptions.materials.resize(gScene->materials.size());
                    gDirty = true;
                }
                if (gJobKind == Job::Collision && result.scene) {
                    gCollision = std::move(result.scene);
                    gDirty = true;
                }
                if (gJobKind == Job::Prepare) {
                    gAsset = std::move(result.asset);
                }
            }
            if (gDirty && gScene && !gJob.valid()) {
                gDirty = false;
                gJobKind = Job::Prepare;
                const auto source = gScene, collision = gCollision;
                const auto options = gOptions;
                gJob = std::async(std::launch::async, [source, collision, options] {
                    PetariNative::HostAllocationScope host;
                    Result result;
                    auto asset = I::prepare(*source, options, &result.error, collision.get());
                    if (asset) {
                        result.asset = std::make_shared< I::Asset >(std::move(*asset));
                    }
                    return result;
                });
            }
        }
        std::array< ImTextureID, 16 > gTextures{};
        std::array< bool, 16 > gTextureValid{};
        std::shared_ptr< I::Scene > gTextureSource;
        std::shared_ptr< I::Asset > gTextureAsset;
        void previewTextures() {
            if ((gTextureSource == gScene && gTextureAsset == gAsset) || !Host::makeTexture()) {
                return;
            }
            gTextureSource = gScene;
            gTextureAsset = gAsset;
            gTextureValid.fill(false);
            if (!gScene) {
                return;
            }
            for (size_t m = 0; m < std::min< size_t >(16, gScene->materials.size()); ++m) {
                const int index = gScene->materials[m].image;
                if (index < 0 || index >= int(gScene->images.size())) {
                    continue;
                }
                const auto& image = gScene->images[index];
                if (image.rgba.empty()) {
                    continue;
                }
                const int w = std::min(256, image.width), h = std::min(256, image.height);
                std::vector< unsigned char > rgba(size_t(w) * h * 4);
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        for (int c = 0; c < 4; ++c) {
                            rgba[(size_t(y) * w + x) * 4 + c] =
                                image.rgba[(size_t(y * image.height / h) * image.width + x * image.width / w) * 4 + c];
                        }
                    }
                }
                if (gAsset && m < gAsset->materials.size() && gAsset->materials[m].texture.format == I::TextureFormat::I8) {
                    for (size_t i = 0; i < rgba.size(); i += 4) {
                        const unsigned value = (77 * rgba[i] + 150 * rgba[i + 1] + 29 * rgba[i + 2] + 128) >> 8;
                        rgba[i] = rgba[i + 1] = rgba[i + 2] = value;
                        rgba[i + 3] = 255;
                    }
                }
                if (!gTextures[m]) {
                    gTextures[m] = Host::makeTexture()(w, h, rgba.data());
                } else if (!Host::updateTexture() || !Host::updateTexture()(gTextures[m], w, h, rgba.data())) {
                    continue;
                }
                gTextureValid[m] = gTextures[m] != 0;
            }
        }
        void preview(const I::Asset& asset) {
            previewTextures();
            const ImVec2 start = ImGui::GetCursorScreenPos(), size{ImGui::GetContentRegionAvail().x, 260};
            ImGui::InvisibleButton("##import-preview", size);
            if (ImGui::IsItemActive()) {
                gYaw += ImGui::GetIO().MouseDelta.x * .012f;
            }
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(start, {start.x + size.x, start.y + size.y}, IM_COL32(4, 22, 35, 255), 7);
            draw->PushClipRect(start, {start.x + size.x, start.y + size.y}, true);
            I::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            for (const auto& part : asset.parts) {
                for (const auto& p : part.mesh.positions) {
                    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
                }
            }
            const I::Vec3 center{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
            const float extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 160.f});
            const float zoom = std::min(size.x * .62f, size.y * .55f) / extent;
            auto rotate = [&](I::Vec3 p) {
                p = {p.x - center.x, p.y - center.y, p.z - center.z};
                const float x = std::cos(gYaw) * p.x + std::sin(gYaw) * p.z, z = -std::sin(gYaw) * p.x + std::cos(gYaw) * p.z;
                return I::Vec3{x, .9f * p.y - .436f * z, .436f * p.y + .9f * z};
            };
            auto screen = [&](I::Vec3 p) { return ImVec2{start.x + size.x * .44f + p.x * zoom, start.y + size.y * .5f - p.y * zoom}; };
            struct Triangle {
                I::Vec3 a, b, c;
                ImU32 color;
                float depth;
                ImTextureID texture = 0;
                ImVec2 ua, ub, uc;
            };
            std::vector< Triangle > triangles;
            size_t total = 0;
            for (const auto& p : asset.parts) {
                total += p.mesh.triangleCount();
            }
            const size_t stride = std::max< size_t >(1, (total + 3999) / 4000);
            for (const auto& part : asset.parts) {
                const auto& color = asset.materials[part.material].color;
                const auto& mesh = part.mesh;
                for (size_t t = 0; t < mesh.triangleCount(); t += stride) {
                    auto a = rotate(mesh.positions[mesh.indices[t * 3]]), b = rotate(mesh.positions[mesh.indices[t * 3 + 1]]),
                         c = rotate(mesh.positions[mesh.indices[t * 3 + 2]]);
                    const I::Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z}, ac{c.x - a.x, c.y - a.y, c.z - a.z};
                    const I::Vec3 n{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
                    const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                    const float shade = .55f + .4f * (length > 0 ? std::abs((n.x * .4f + n.y * .8f + n.z * .45f) / length) : 0);
                    triangles.push_back({a, b, c, ImGui::ColorConvertFloat4ToU32({color[0] * shade, color[1] * shade, color[2] * shade, .95f}),
                                         (a.z + b.z + c.z) / 3});
                    if (part.material < int(gTextures.size()) && gTextureValid[part.material] && asset.materials[part.material].donor < 0 &&
                        mesh.uvs.size() == mesh.positions.size() * 2) {
                        auto& tri = triangles.back();
                        tri.texture = gTextures[part.material];
                        auto uv = [&](int corner) {
                            const auto i = mesh.indices[t * 3 + corner] * 2;
                            return ImVec2{mesh.uvs[i], mesh.uvs[i + 1]};
                        };
                        tri.ua = uv(0);
                        tri.ub = uv(1);
                        tri.uc = uv(2);
                    }
                }
            }
            std::sort(triangles.begin(), triangles.end(), [](const auto& a, const auto& b) { return a.depth < b.depth; });
            for (const auto& t : triangles) {
                if (t.texture) {
                    draw->AddImageQuad(t.texture, screen(t.a), screen(t.b), screen(t.c), screen(t.c), t.ua, t.ub, t.uc, t.uc, t.color);
                } else {
                    draw->AddTriangleFilled(screen(t.a), screen(t.b), screen(t.c), t.color);
                }
            }
            const auto zero = screen(rotate({0, 0, 0}));
            draw->AddCircle(zero, 4, IM_COL32(85, 240, 230, 255), 0, 2);
            // Mario-sized reference silhouette (roughly 150 game units tall).
            const float height = 150 * zoom, foot = start.y + size.y * .8f, x = start.x + size.x * .86f;
            const ImU32 red = IM_COL32(246, 125, 108, 255);
            draw->AddCircleFilled({x, foot - height * .87f}, height * .13f, red);
            draw->AddRectFilled({x - height * .14f, foot - height * .72f}, {x + height * .14f, foot - height * .28f}, red, height * .07f);
            draw->AddLine({x - height * .08f, foot - height * .3f}, {x - height * .16f, foot}, red, std::max(1.f, height * .12f));
            draw->AddLine({x + height * .08f, foot - height * .3f}, {x + height * .16f, foot}, red, std::max(1.f, height * .12f));
            draw->AddText({start.x + 12, start.y + 10}, IM_COL32(165, 205, 218, 255), "Drag to orbit  |  dot = origin");
            draw->AddText({x - 30, start.y + size.y - 24}, IM_COL32(245, 171, 152, 255), "Mario ~150u");
            draw->PopClipRect();
            ImGui::TextDisabled("Size: %.0f x %.0f x %.0f units", hi.x - lo.x, hi.y - lo.y, hi.z - lo.z);
        }
    }  // namespace
    void openImportSheet(const char* path) {
        gOpen = true;
        if (path && *path) {
            loadPath(path, false);
        } else {
            choose(false);
        }
    }
    void drawImportSheet() {
        static const char* test = std::getenv("PETARI_EDITOR_IMPORT_SHEET");
        static int testFrames = 0;
        if (test && mode() == Mode::Editing && model() && ++testFrames == 10) {
            openImportSheet(test);
        }
        service();
        if (gOpen) {
            gOpen = false;
            ImGui::OpenPopup("Import model");
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {.5f, .5f});
        ImGui::SetNextWindowSize({std::min(920.f, ImGui::GetMainViewport()->Size.x - 48), std::min(640.f, ImGui::GetMainViewport()->Size.y - 48)});
        ImGui::PushStyleColor(ImGuiCol_PopupBg, {.02f, .20f, .31f, 1});
        ImGui::PushStyleColor(ImGuiCol_Border, {.40f, .86f, 1, .85f});
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, {0, .06f, .12f, .72f});
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 14});
        const bool open = ImGui::BeginPopupModal("Import model", nullptr, ImGuiWindowFlags_NoResize);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        if (!open) {
            return;
        }
        ImGui::BeginDisabled(gJob.valid());
        if (ImGui::Button("Choose model...")) {
            choose(false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted(std::filesystem::path(gPath).filename().string().c_str());
        if (!Host::fileDialog()) {
            static char path[2048]{};
            ImGui::InputText("Path", path, sizeof(path));
            ImGui::SameLine();
            if (ImGui::Button("Load") && !gJob.valid()) {
                loadPath(path, false);
            }
        }
        const float bodyHeight = std::max(180.f, ImGui::GetContentRegionAvail().y - 90);
        ImGui::BeginTable("import-layout", 2, ImGuiTableFlags_SizingStretchProp);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, .48f);
        ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch, .52f);
        ImGui::TableNextColumn();
        ImGui::BeginChild("import-visual", {0, bodyHeight});
        if (gAsset) {
            preview(*gAsset);
        }
        if (test && gAsset && !gJob.valid() && !gDirty && gError.empty()) {
            static int ready = 0;
            if (++ready == 10) {
                PetariNative::EfbDump::mark("import-sheet");
            }
            if (ready == 45) {
                I::requestPlacement(gAsset);
                ImGui::CloseCurrentPopup();
            }
        }
        if (gAsset) {
            size_t triangles = 0, collision = 0, texture = 0;
            for (const auto& p : gAsset->parts) {
                triangles += p.mesh.triangleCount();
                collision += p.collision.triangleCount();
            }
            for (const auto& m : gAsset->materials) {
                texture += m.texture.data.size();
            }
            ImGui::TextWrapped("%zu part%s  |  %zu triangles\n%zu collision triangles  |  %.2f MiB textures", gAsset->parts.size(),
                               gAsset->parts.size() == 1 ? "" : "s", triangles, collision, texture / 1048576.0);
            for (const auto& warning : gAsset->warnings) {
                ImGui::TextWrapped("%s", warning.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild("import-options", {0, bodyHeight});
        ImGui::PushItemWidth(180);
        if (gScene) {
            drawSection("Transform");
            bool changed = ImGui::InputFloat("Scale", &gOptions.scale, 1, 10, "%.3f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Game units per source unit");
            }
            int up = int(gOptions.up), origin = int(gOptions.origin);
            changed |= ImGui::Combo("Up axis", &up, "Y up\0Z up\0X up\0");
            gOptions.up = I::UpAxis(up);
            changed |= ImGui::Combo("Origin", &origin, "Keep source origin\0Centre\0Bottom centre\0");
            gOptions.origin = I::Origin(origin);
            drawSection("Collision and detail");
            int collision = int(gOptions.collision);
            changed |= ImGui::Combo("Collision", &collision, "Full mesh\0Simplified mesh\0Bounds hull\0None\0Separate file\0");
            gOptions.collision = I::Collision(collision);
            if (gOptions.collision == I::Collision::Simplified) {
                changed |= ImGui::SliderFloat("Collision triangles", &gOptions.collisionRatio, .01f, 1, "%.2f of original");
            }
            if (gOptions.collision == I::Collision::External) {
                ImGui::BeginDisabled(gJob.valid());
                if (ImGui::Button("Choose collision model...")) {
                    choose(true);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted(std::filesystem::path(gCollisionPath).filename().string().c_str());
            }
            changed |= ImGui::Checkbox("Generate middle and low detail", &gOptions.generateLods);
            changed |= ImGui::Checkbox("Generate downward gravity", &gOptions.gravity);
            if (ImGui::CollapsingHeader("Materials and collision surfaces", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (size_t i = 0; i < gOptions.materials.size(); ++i) {
                    ImGui::PushID(int(i));
                    auto& options = gOptions.materials[i];
                    ImGui::SeparatorText(gScene->materials[i].name.c_str());
                    const auto& palette = Terrain::materialPalette();
                    if (ImGui::BeginCombo("Look", options.donor < 0 ? "Imported texture / colour" : palette[options.donor].label.c_str())) {
                        if (ImGui::Selectable("Imported texture / colour", options.donor < 0)) {
                            options.donor = -1;
                            changed = true;
                        }
                        for (size_t p = 0; p < palette.size(); ++p) {
                            if (ImGui::Selectable(palette[p].label.c_str(), options.donor == int(p))) {
                                options.donor = p;
                                options.surface.sound = palette[p].sound;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (options.donor < 0) {
                        const I::TextureFormat formats[] = {I::TextureFormat::Auto, I::TextureFormat::CMPR, I::TextureFormat::RGB5A3,
                                                            I::TextureFormat::I8};
                        int format = 0;
                        for (int f = 0; f < 4; ++f) {
                            if (formats[f] == options.format) {
                                format = f;
                            }
                        }
                        if (ImGui::Combo("Texture", &format, "Auto\0CMPR (compact colour)\0RGB5A3 (colour + alpha)\0I8 (greyscale)\0")) {
                            options.format = formats[format];
                            changed = true;
                        }
                    }
                    const auto& floors = Terrain::floorCodeNames();
                    if (ImGui::BeginCombo("Surface", floors[options.surface.floor].c_str())) {
                        for (size_t f = 0; f < floors.size(); ++f) {
                            if (ImGui::Selectable(floors[f].c_str(), options.surface.floor == int(f))) {
                                options.surface.floor = f;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
            }
            if (changed) {
                gDirty = true;
            }
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndTable();
        if (gJob.valid()) {
            ImGui::TextDisabled("Preparing model...");
        }
        if (!gError.empty()) {
            ImGui::TextWrapped("%s", gError.c_str());
        }
        ImGui::BeginDisabled(!gAsset || gJob.valid() || gDirty || !gError.empty() || !model());
        if (ImGui::Button("Import at play marker", {210, 0})) {
            I::requestPlacement(gAsset);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        if (!model()) {
            ImGui::TextWrapped("Enter Edit in a galaxy to place this model.");
        }
        ImGui::EndPopup();
    }
}  // namespace PetariEditor::Ui
