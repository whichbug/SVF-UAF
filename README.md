## Thanks to the original authors of SVF/Saber

Here is our implementation of use-after-free checker based on SVF.
The original implementation is here: https://github.com/SVF-tools/SVF

Run the use-after-free checker as:

```bash
saber -uaf -mempar=inter-disjoint -stat=false -no-global *.bc
``` 

