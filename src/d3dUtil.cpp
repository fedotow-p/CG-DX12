#include "../h/d3dUtil.h"
#include "../h/ThrowIfFailed.h"

namespace d3dUtil
{
    Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
        const std::wstring& filename,
        const D3D_SHADER_MACRO* defines,
        const std::string& entrypoint,
        const std::string& target)
    {
        UINT compileFlags = 0;
#ifdef _DEBUG
        compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        Microsoft::WRL::ComPtr<ID3DBlob> byteCode = nullptr;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;

        HRESULT hr = D3DCompileFromFile(
            filename.c_str(),
            defines,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entrypoint.c_str(),
            target.c_str(),
            compileFlags,
            0,
            &byteCode,
            &errors
        );

        if (errors != nullptr)
        {
            std::string errorStr = "Shader Compile Error:\n";
            errorStr += (char*)errors->GetBufferPointer();
            OutputDebugStringA(errorStr.c_str());
        }

        if (FAILED(hr))
        {
            if (errors)
            {
                MessageBoxA(0,
                    (char*)errors->GetBufferPointer(),
                    "Shader Compile Error",
                    MB_OK);
            }
            ThrowIfFailed(hr);
        }
        return byteCode;
    }

    UINT CalcConstantBufferByteSize(UINT byteSize)
    {
        // Constant buffers must be a multiple of 256 bytes.
        // Round up to nearest multiple of 256.
        return (byteSize + 255) & ~255;
    }
}