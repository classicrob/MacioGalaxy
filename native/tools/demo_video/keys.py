"""Build a PETARI_INPUT_SCRIPT key script frame by frame, then write it compressed.

    from keys import Plan
    p = Plan()
    p.wait(30)                            # nothing held
    p.hold(20, "W")                       # run
    p.hold(12, "W", "Space")              # jump
    p.overlay("L", start=50, frames=75)   # camera orbit held over whatever moves happen then
    p.write("keys.txt")

Key names are controls.txt names. Default bindings (native/CONTROLS.md, docs/dev/ODYSSEY_CAMERA.md):
W A S D move (camera-relative), Space jump (dive during a ground pound), LeftShift crouch / ground pound
(long jump: run + Shift + Space), R throw Cappy, F spin; camera: J / L orbit, I / K pitch, Z / X zoom.
"""


class Plan:
    def __init__(self):
        self.frames = []  # one set of keys per frame

    @property
    def now(self):
        return len(self.frames)

    def wait(self, n):
        self.hold(n)

    def hold(self, n, *keys):
        for _ in range(n):
            self.frames.append(set(keys))

    def overlay(self, *keys, start, frames):
        """Add keys to frames already planned (e.g. a camera key across a stretch of movement)."""
        for f in range(start, min(start + frames, len(self.frames))):
            self.frames[f] |= set(keys)

    def write(self, path):
        lines = []
        for k in self.frames:
            key = " ".join(sorted(k))
            if lines and lines[-1][1] == key:
                lines[-1][0] += 1
            else:
                lines.append([1, key])
        with open(path, "w") as fh:
            for n, k in lines:
                fh.write(f"{n} {k}\n".rstrip() + "\n")
        return len(self.frames)


def script_frames(path):
    """Total frames of a key script file (the sum of its lines' counts)."""
    total = 0
    with open(path) as fh:
        for line in fh:
            words = line.split("#")[0].split()
            if words:
                total += int(words[0])
    return total
