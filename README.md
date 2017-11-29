## Thanks to the original authors of SVF/Saber

Here is our implementation of use-after-free checker based on SVF and LLVM 3.6.
The original implementation is here: https://github.com/SVF-tools/SVF

Run the use-after-free checker as:

```bash
saber -uaf -mempar=inter-disjoint -stat=false -no-global *.bc
```

You can use bitcode of cpu2000 programs in tests/tests4uaf/ for testing. These are llvm3.6 bitcode and loops in a function have been unrolled.

