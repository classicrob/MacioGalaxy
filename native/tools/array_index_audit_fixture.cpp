// Fixture for native/tools/array_index_audit.py --selftest. Never compiled into anything: it holds the
// patterns the audit must flag (the F1 -> Mods crash among them) and the ones it must not.
struct Item { const char* label; };
struct View { int count = 0; Item items[8]; int lineCount = 0; };

int expectCompute(View& view, int mods, int i, int* table) {
    view.items[mods + 3].label = "Back";      // EXPECT COMPUTED: the original crash (5 mods -> items[8])
    view.items[i * 2].label = "x";            // EXPECT COMPUTED
    view.items[9].label = "y";                // EXPECT CONST-OOB
    view.items[3].label = "ok";               // constant, in range: not reported
    view.items[i].label = "z";                // VAR: counted only
    char name[16];
    name[sizeof(name) - 1] = '\0';            // sizeof is constant: not reported
    table[mods + 1] = 0;                      // pointer base: size unknown, not reported
    for (int k = 0; k < mods; ++k) view.items[k].label = "loop";  // EXPECT LOOP-RUNTIME (VAR, bound is a runtime value)
    for (int k = 0; k < 8; ++k) view.items[k].label = "const";    // EXPECT LOOP-CONST (VAR, literal bound)
    view.items[table[0] + view.items[0].label[0]].label = "n";   // EXPECT COMPUTED (nested subscript inside an index)
    return view.items[mods + 1].label[0];     // audit-ok: fixture, shows a suppression  (EXPECT COMPUTED, suppressed)
}
