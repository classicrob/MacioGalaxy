# Writes OUT: the shipped catalog files as C++ raw string literals (native/editor/CMakeLists.txt).
file(READ "${GENERATED}" generated)
file(READ "${CURATED}" curated)
file(WRITE "${OUT}" "// Generated from native/editor/catalog/*.json by embed_catalog.cmake. Do not edit.\n"
    "extern const char* const kPetariCatalogGenerated;\nextern const char* const kPetariCatalogCurated;\n"
    "const char* const kPetariCatalogGenerated = R\"__catalog__(${generated})__catalog__\";\n"
    "const char* const kPetariCatalogCurated = R\"__catalog__(${curated})__catalog__\";\n")
