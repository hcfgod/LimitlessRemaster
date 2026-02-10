## shaderc runtime DLL drop folder

Place any Windows runtime DLLs that your shaderc integration needs in this folder.

On Windows builds, Premake adds a post-build step for `Sandbox` and `Test` that copies
everything in this directory next to the built executables (the `%{cfg.targetdir}` output folder).

This keeps the repo self-contained and avoids relying on global PATH setup.

