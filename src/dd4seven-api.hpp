/*
 * API for dd4seven-api.dll
 */

#include <dxgi.h>
#include <dxgi1_2.h>


extern "C" {

/**
 * Works like IDXGIOutput1::DuplicateOutput
 *
 * Might return the following error codes:
 * - E_INVALIDARG: output is NULL, duplication is NULL, device is no D3D10/D3D11 device
 * - DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: If the DWM is not cooperating with us
 * - E_FAILED: If something went wrong internally
 */
HRESULT
__stdcall
DuplicateOutput(IDXGIOutput *output, IUnknown *device, IDXGIOutputDuplication **duplication);

} // extern "C"