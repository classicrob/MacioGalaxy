// Native definitions of the J3D functions that the game overrides in Overwrite.cpp.
// The Wii definitions stay in src/Game/System/Overwrite.cpp under !PETARI_NATIVE.
#include <petari/j3d_overrides.hpp>
#include <JSystem/J3DGraphBase/J3DFifo.hpp>
#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <JSystem/J3DGraphBase/J3DShapeMtx.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>

namespace {
    PetariNative::J3D::ShapePacketTexMtxLoader sShapePacketTexMtxLoader = nullptr;
    bool sImmediateMatrixLoads = false;
}  // namespace

void PetariNative::J3D::setShapePacketTexMtxLoader(ShapePacketTexMtxLoader loader) {
    sShapePacketTexMtxLoader = loader;
}

bool PetariNative::J3D::setImmediateMatrixLoads(bool enabled) {
    const bool previous = sImmediateMatrixLoads;
    sImmediateMatrixLoads = enabled;
    return previous;
}

bool PetariNative::J3D::immediateMatrixLoads() {
    return sImmediateMatrixLoads;
}

void J3DShapeMtx::loadMtxIndx_PNGP(int slot, u16 index) const {
    j3dSys.loadPosMtxIndx(slot, index);
    j3dSys.loadNrmMtxIndx(slot, index);
    if (sShapePacketTexMtxLoader != nullptr) {
        sShapePacketTexMtxLoader(j3dSys.getShapePacket(), slot, index);
    }
}
