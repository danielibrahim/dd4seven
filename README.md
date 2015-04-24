Desktop Duplication for Windows 7
=================================

This project aims to implement parts of the [Desktop Duplication API](https://msdn.microsoft.com/en-us/library/windows/desktop/hh404487%28v=vs.85%29.aspx) for Windows 7.

---

**Warning**: DO **NOT** USE IN PRODUCTION! You have been warned.

---

What works
----------
* Acquiring desktop and mouse cursor images using the `IDXGIOutputDuplication` interface.
* It doesn't matter whether D3D10 or D3D11 is used, and no DXGI upgrade is required.

What's broken
-------------
* A running DWM (= Aero theme) is required.
* The reported timings are improvised and completely unusable for synchronization.
* Change and move regions are not supported.
* All other metadata or reported information is garbage, too.
* Mouse cursor and desktop updates are always delivered together.
* The 64bit Debug builds are mysteriously crashing (though I'm tempted to blame the compiler for this).
  The relese builds work fine.
* It will totally break everything but Win7. But that's OK, because Win8 and later
  contain the functionality natively. Just remember to uninstall before upgrading.

How to install
--------------
* There are no precompiled binaries. You can build it yourself using a recent MinGW-w64 toolchain.
  I personally only tried cross-compiling from my Fedora system, but you can probably adapt the Makefile
  to build on Cygwin or MinGW.
* For **32bit (x86)** systems: Drop the x86 binaries (`dd4seven-dwm.dll`, `dd4seven-api.dll`) into `%WINDIR%\System32`.
* For **64bit (amd64)** systems: Drop the amd64 binaries (`dd4seven-dwm.dll`, `dd4seven-api.dll`) into `%WINDIR\System32`.
  and the x86 api binary (`dd4seven-api.dll`) into `%WINDIR%\SysWOW64`.
* Add `dd4seven-dwm.dll` to the [AppInitDLLs registry value](https://msdn.microsoft.com/en-us/library/dd744762(v=VS.85).aspx).
* Restart the DWM (i.e. run `tskill dwm.exe`, restart the `UxSms` service, or just log off and on again)

How to use in applications
--------------------------
Use the `DuplicateOutput` function, exported by `dd4seven-api.dll`, as replacement for `IDXGIOutput1::DuplicateOutput`:

    ID3D11Device *device; // D3D10 is also okay
    IDXGIOutput *output;   // on Win7
    IDXGIOutput1 *output1; // on Win8
    IDXGIOutputDuplication *dupl = nullptr;

    HMODULE ddApi = LoadLibraryA("dd4seven-api.dll");
    if (ddApi) {
        // Win7
        HRESULT (__stdcall *duplicate)(IDXGIOutput *output, IUnknown *device, IDXGIOutputDuplication **dupl);
        duplicate = (decltype(duplicate))GetProcAddress(ddApi, "DuplicateOutput");

        duplicate(output, device, &dupl);
    } else {
        // Win8
        output1->DuplicateOutput(device, &dupl);
    }


Credits
-------
This project uses the MinHook library by Tsuda Kageyu. See `minhook/LICENSE.txt` for details.
