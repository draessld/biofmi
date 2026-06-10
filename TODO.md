# Open Issues

*(none — all tracked issues resolved)*

---

## Future Work

**Arbitrary pattern lengths** (`src/cpp/lib/index/index.cpp`):
- Currently `|P|` must be a multiple of `l+1`. Supporting arbitrary lengths requires
  a different lookup strategy for partial chunks.
