`BuildConfig.h`: the compile-time settings of the firmware, for `src/`. The
libraries in `lib/` do not see this folder; a constant one of them needs is its
own, and `src/main.cpp` checks it against the setting with a `static_assert`.
