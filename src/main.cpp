
#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#ifndef SOURCE_PATH
#define SOURCE_PATH L"Wrong Source Path"
#endif

#ifndef ASSETS_PATH
#define ASSETS_PATH L"Wrong Assets Path"
#endif

#include <algorithm>
#include <iostream>
#include <WindowsX.h>
#include <windows.h>
#include <dxgi.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include "d3dx12.h"
#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <winuser.h>
#include <wrl.h>
#include <sstream>
#include <fstream>
#include <DirectXMesh.h>
#include <WaveFrontReader.h>

struct App {

    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    static inline std::string HrToString(HRESULT hr)
    {
        char s_str[64] = {};
        sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
        return std::string(s_str);
    }

    class HrException : public std::runtime_error
    {
    public:
        HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
        HRESULT Error() const { return m_hr; }
    private:
        const HRESULT m_hr;
    };

    static inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw HrException(hr);
        }
    }

    static void GetHardwareAdapter(const ComPtr<IDXGIFactory4>& pFactory, ComPtr<IDXGIAdapter1>& pAdapter, bool userChoice = false)
    {
        ComPtr<IDXGIFactory6> factory6;
        if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
        {
            std::cout << "Adapters found:\n";
            ComPtr<IDXGIAdapter1> adapter;
            for (int i=0; SUCCEEDED(factory6->EnumAdapters1(i, adapter.GetAddressOf())); ++i)
            {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                std::wcout << i << ": " << desc.Description << '\n';
            }
        }

        if (userChoice) {
            for(;;) {
                UINT choice = 0;
                std::cin >> choice;

                if (SUCCEEDED(factory6->EnumAdapters1(choice, pAdapter.GetAddressOf())))
                {
                    return;
                } else 
                {
                    std::cerr << "Choice " << choice << " seems wrong. Try again: \n";
                }
            }
        } else {
            for (int i=0; SUCCEEDED(factory6->EnumAdapters1(i, pAdapter.GetAddressOf())); ++i)
            {
                DXGI_ADAPTER_DESC1 desc;
                pAdapter->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                    // Don't pick software
                    continue;
                }

                std::wcout << "Picked: " << desc.Description << '\n';
                return;                
            }
        }

        std::cerr << "Failed to pick an adapter." << std::endl;
    }

    static const UINT SwapChainBufferCount = 2;

    _declspec(align(256u)) struct SceneConstantBuffer
    {
        DirectX::XMFLOAT4X4 World;
        DirectX::XMFLOAT4X4 WorldView;
        DirectX::XMFLOAT4X4 WorldViewProj;
        uint32_t   DrawMeshlets;
        uint32_t   IndicesCount;
        uint32_t   VerticesCount;
    };

    // DXGI stuff
    HINSTANCE           _hAppInstance = nullptr;
    HWND                _hMainWindow = nullptr;
    std::wstring        _MainWindowCaption = L"Mesh Shaders";
    LONG                _winWidth = 1200;
    LONG                _winHeight = 900;
    ComPtr<ID3D12Debug> _debugController;
    CD3DX12_VIEWPORT    _viewport{ 0.0f, 0.0f, static_cast<float>(_winWidth), static_cast<float>(_winHeight) };
    CD3DX12_RECT        _scissorRect{ 0, 0, _winWidth, _winHeight };

    // D3D12 stuff
    ComPtr<ID3D12Device2>            _device;
    ComPtr<ID3D12CommandQueue>      _commandQueue;
    ComPtr<IDXGISwapChain3>         _swapChain;
    ComPtr<ID3D12DescriptorHeap>    _rtvHeap;
    UINT                            _rtvDescriptorSize;
    ComPtr<ID3D12DescriptorHeap>    _dsvHeap;
    UINT                            _dsvDescriptorSize;

    ComPtr<ID3D12Resource>              _renderTargets[SwapChainBufferCount];
    ComPtr<ID3D12CommandAllocator>      _commandAllocator[SwapChainBufferCount];
    ComPtr<ID3D12GraphicsCommandList6>  _commandList[SwapChainBufferCount];

    ComPtr<ID3D12Resource>          _depthStencil;
    ComPtr<ID3D12Resource>          _depthStencilView;

    ComPtr<ID3D12Resource>          _constantBuffer;
    ComPtr<ID3D12Resource>          _constantBufferView;
    UINT8*                          _cbvDataBegin;

    ComPtr<ID3D12RootSignature>    _rootSignature;
    ComPtr<ID3D12PipelineState>    _pipelineState;

    ComPtr<ID3D12Resource>          _indexBufferResource;
    D3D12_INDEX_BUFFER_VIEW         _indexBufferView;
    uint32_t                        _indicesCount;
    ComPtr<ID3D12Resource>          _vertexBufferResource;
    D3D12_VERTEX_BUFFER_VIEW        _vertexBufferView;
    uint32_t                        _verticesCount;

    // Runtime
    UINT _currentSwapChainBufferIndex;
    UINT _frameId = 0;
    ComPtr<ID3D12Fence>            _frameProgressFence[SwapChainBufferCount];

    App(HINSTANCE instance) 
    : _hAppInstance(instance) {
        if (instance == NULL) {
            MessageBox(0, L"Instance is null.", 0, 0);
        }
        InitMainWindow();
        InitD3D12();
        InitSample();
    }

    bool InitMainWindow() 
    {
        WNDCLASS wc;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = MainWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = _hAppInstance;
        wc.hIcon = LoadIcon(0, IDI_APPLICATION);
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
        wc.lpszMenuName = 0;
        wc.lpszClassName = L"MainWnd";
        if (!RegisterClass(&wc)) {
            MessageBox(0, L"RegisterClass Failed.", 0, 0);
            return false;
        }
        RECT R = {0, 0, _winWidth, _winHeight };
        AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
        int width = R.right - R.left;
        int height = R.bottom - R.top;
        _hMainWindow = CreateWindow(
            L"MainWnd",
            _MainWindowCaption.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            width, height,
            NULL, NULL,
            _hAppInstance, 0
        );
        if (! _hMainWindow ) {
            DWORD error = GetLastError();
            std::wostringstream woss;
            woss << L"Failed CreateWindow: " << error;
            MessageBox(0, woss.str().c_str(), 0, 0);
            return false;
        }
        ShowWindow(_hMainWindow, SW_SHOW);
        UpdateWindow(_hMainWindow);
        return true;
    }

    bool InitD3D12()
    {
        HRESULT hr;
        UINT dxgiFactoryFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&_debugController))))
        {
            _debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

            ComPtr<ID3D12Debug1> spDebugController1;
            ThrowIfFailed(_debugController->QueryInterface(IID_PPV_ARGS(&spDebugController1)));
            spDebugController1->SetEnableGPUBasedValidation(true);
        }
#endif

        ComPtr<IDXGIFactory4> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
        
        // Create device
        {
            ComPtr<IDXGIAdapter1> hardwareAdapter;
            GetHardwareAdapter(factory, hardwareAdapter);

            ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&_device)));
        }

        // Describe and create the command queue.
        {
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

            ThrowIfFailed(_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_commandQueue)));
        }

        // Describe and create the swap chain.
        {
            DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
            swapChainDesc.BufferCount = SwapChainBufferCount;
            swapChainDesc.Width = _winWidth;
            swapChainDesc.Height = _winHeight;
            swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swapChainDesc.SampleDesc.Count = 1;

            ComPtr<IDXGISwapChain1> swapChain;
            ThrowIfFailed(factory->CreateSwapChainForHwnd(
                _commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
                _hMainWindow,
                &swapChainDesc,
                nullptr,
                nullptr,
                &swapChain
            ));

            // Don't support fullscreen transitions for now...
            ThrowIfFailed(factory->MakeWindowAssociation(_hMainWindow, DXGI_MWA_NO_ALT_ENTER));
            ThrowIfFailed(swapChain.As(&_swapChain));
            _currentSwapChainBufferIndex = _swapChain->GetCurrentBackBufferIndex();
            
        }

        // Create descriptor heaps.
        {
            D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
            rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
            rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            ThrowIfFailed(_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap)));
            _rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
            dsvHeapDesc.NumDescriptors = 1;
            dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            ThrowIfFailed(_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&_dsvHeap)));
            _dsvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        }

        // Create frame resources.
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(_rtvHeap->GetCPUDescriptorHandleForHeapStart());

            // Create RTV for each Backbuffer
            for(UINT i = 0; i < SwapChainBufferCount; ++i) {
                ThrowIfFailed(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_renderTargets[i])));
                _device->CreateRenderTargetView(_renderTargets[i].Get(), nullptr, rtvHandle);
                rtvHandle.Offset(1, _rtvDescriptorSize);
            }
        }

        // Create the depth stencil view
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
            depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
            depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

            D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
            depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
            depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
            depthOptimizedClearValue.DepthStencil.Stencil = 0;

            const CD3DX12_HEAP_PROPERTIES depthStencilHeapProps(D3D12_HEAP_TYPE_DEFAULT);
            const CD3DX12_RESOURCE_DESC depthStencilTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, _winWidth, _winHeight, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

            ThrowIfFailed(_device->CreateCommittedResource(
                &depthStencilHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &depthStencilTextureDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &depthOptimizedClearValue,
                IID_PPV_ARGS(&_depthStencil)
            ));

            _device->CreateDepthStencilView(_depthStencil.Get(), &depthStencilDesc, _dsvHeap->GetCPUDescriptorHandleForHeapStart());
        }

        // Create the constant buffer
        {
            const UINT64 constantBufferSize = sizeof(SceneConstantBuffer) * SwapChainBufferCount;
            
            const CD3DX12_HEAP_PROPERTIES constantBufferHeapProps(D3D12_HEAP_TYPE_UPLOAD);
            const CD3DX12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);

            ThrowIfFailed(_device->CreateCommittedResource(
                &constantBufferHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &constantBufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&_constantBuffer)
            ));

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
            cbvDesc.BufferLocation = _constantBuffer->GetGPUVirtualAddress();
            cbvDesc.SizeInBytes = constantBufferSize;

            // Map and initialize the constant buffer. We don't unmap this until the
            // app closes. Keeping things mapped for the lifetime of the resource is okay.
            CD3DX12_RANGE readRange(0, 0);
            ThrowIfFailed(_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&_cbvDataBegin)));
        }

        for (int i = 0; i < SwapChainBufferCount; ++i ) {
            ThrowIfFailed(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_commandAllocator[i])));
        }

        return true;
    }

    void InitSample()
    {
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_5 };
        if (FAILED(_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) || (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_5))
        {
            OutputDebugString(L"Error: Shader Model 6.5 is not supported\n");
            throw std::runtime_error("Shader Model 6.5 is not supported\n");
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS7 features {};
        if (FAILED(_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features))) || (features.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED))
        {
            OutputDebugString(L"Error: Shader Model 6.5 is not supported\n");
            throw std::runtime_error("Shader Model 6.5 is not supported\n");
        }

        // Create pipeline state 
        {
            constexpr char* meshShaderPath = "MeshletMS.cso";
            constexpr char* pixelShaderPath = "MeshletPS.cso";
            struct 
            {
                ComPtr<ID3DBlob> code;
                ComPtr<ID3DBlob> errors;
            } meshShader, pixelShader;

            // Read mesh shader code
            {
                std::ifstream shaderCodeFile(meshShaderPath, std::ios::binary | std::ios::ate);
                if (!shaderCodeFile.is_open()) {
                    throw std::runtime_error("Cannot open MeshShader code file.");
                }

                shaderCodeFile.seekg(0, std::ios::end);
                size_t size = shaderCodeFile.tellg();
                shaderCodeFile.seekg(0);
                D3DCreateBlob(size, meshShader.code.GetAddressOf());
                shaderCodeFile.read(reinterpret_cast<char*>(meshShader.code->GetBufferPointer()), size);

                shaderCodeFile.close();
            }


            // Read pixel shader code
            {
                std::ifstream shaderCodeFile(pixelShaderPath, std::ios::binary | std::ios::ate);
                if (!shaderCodeFile.is_open()) {
                    throw std::runtime_error("Cannot open MeshShader code file.");
                }

                shaderCodeFile.seekg(0, std::ios::end);
                size_t size = shaderCodeFile.tellg();
                shaderCodeFile.seekg(0);
                D3DCreateBlob(size, pixelShader.code.GetAddressOf());
                shaderCodeFile.read(reinterpret_cast<char*>(pixelShader.code->GetBufferPointer()), size);

                shaderCodeFile.close();
            }

            // Pull root signature frm the precompiled mesh shader
            ThrowIfFailed(_device->CreateRootSignature(0, meshShader.code->GetBufferPointer(), meshShader.code->GetBufferSize(), IID_PPV_ARGS(&_rootSignature)));

            D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature          = _rootSignature.Get();
            psoDesc.MS                      = { meshShader.code->GetBufferPointer(), meshShader.code->GetBufferSize() };
            psoDesc.PS                      = { pixelShader.code->GetBufferPointer(), pixelShader.code->GetBufferSize() };
            psoDesc.NumRenderTargets        = 1;
            psoDesc.RTVFormats[0]           = _renderTargets[0]->GetDesc().Format;
            psoDesc.DSVFormat               = _depthStencil->GetDesc().Format;
            psoDesc.RasterizerState         = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // CW front; cull back
            psoDesc.BlendState              = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // Opaque
            psoDesc.DepthStencilState       = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // Less-equal depth test w/ writes; no stencil
            psoDesc.SampleMask              = UINT_MAX;
            psoDesc.SampleDesc              = DefaultSampleDesc();
            
            auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);

            D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
            streamDesc.pPipelineStateSubobjectStream = &psoStream;
            streamDesc.SizeInBytes                   = sizeof(psoStream);

            ThrowIfFailed(_device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&_pipelineState)));
        }

        // Create command list
        {
            for (int i = 0; i < SwapChainBufferCount; ++i ){
                ThrowIfFailed(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocator[i].Get(), _pipelineState.Get(), IID_PPV_ARGS(&_commandList[i])));
                // Command lists are created in recording state, but there's nothing to record yet. Close for now.
                ThrowIfFailed(_commandList[i]->Close());
            }
        }

        // Get some asset to render
        {
            WaveFrontReader<uint32_t> wfReader;
            ThrowIfFailed(wfReader.Load(ASSETS_PATH L"dragon.obj", true));

            _indicesCount = wfReader.indices.size();
            _verticesCount = wfReader.vertices.size();

            auto indexDesc = CD3DX12_RESOURCE_DESC::Buffer(wfReader.indices.size() * sizeof(wfReader.indices[0]));
            auto vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(wfReader.vertices.size() * sizeof(wfReader.vertices[0]));
            
            auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &indexDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(_indexBufferResource.GetAddressOf())));
            ThrowIfFailed(_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(_vertexBufferResource.GetAddressOf())));

            _indexBufferView.BufferLocation = _indexBufferResource->GetGPUVirtualAddress();
            _indexBufferView.Format = DXGI_FORMAT_R32_UINT;
            _indexBufferView.SizeInBytes = wfReader.indices.size() * sizeof(wfReader.indices[0]);

            _vertexBufferView.BufferLocation = _vertexBufferResource->GetGPUVirtualAddress();
            _vertexBufferView.SizeInBytes = wfReader.vertices.size() * sizeof(wfReader.vertices[0]);
            _vertexBufferView.StrideInBytes = sizeof(wfReader.vertices[0]);

            ComPtr<ID3D12Resource> indexUpload;
            ComPtr<ID3D12Resource> vertexUpload;

            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            ThrowIfFailed(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &indexDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(indexUpload.GetAddressOf())));
            ThrowIfFailed(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(vertexUpload.GetAddressOf())));

            // Move index buffer to upload
            {
                byte* memory = nullptr;
                indexUpload->Map(0, nullptr, reinterpret_cast<void**>(&memory));
                std::memcpy(memory, wfReader.indices.data(), sizeof(wfReader.indices[0]) * wfReader.indices.size());
                indexUpload->Unmap(0, nullptr);
            }

            // Move vertex buffer to upload
            {
                byte* memory = nullptr;
                vertexUpload->Map(0, nullptr, reinterpret_cast<void**>(&memory));
                std::memcpy(memory, wfReader.vertices.data(), sizeof(wfReader.vertices[0]) * wfReader.vertices.size());
                vertexUpload->Unmap(0, nullptr);
            }

            const uint8_t swapBuffer = _frameId % SwapChainBufferCount;

            _commandList[swapBuffer]->Reset(_commandAllocator[swapBuffer].Get(), nullptr);

            _commandList[swapBuffer]->CopyResource(_indexBufferResource.Get(), indexUpload.Get());
            const auto indexCopyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(_indexBufferResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            _commandList[swapBuffer]->ResourceBarrier(1, &indexCopyBarrier);

            _commandList[swapBuffer]->CopyResource(_vertexBufferResource.Get(), vertexUpload.Get());
            const auto vertexCopyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(_vertexBufferResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            _commandList[swapBuffer]->ResourceBarrier(1, &vertexCopyBarrier);

            ThrowIfFailed(_commandList[swapBuffer]->Close());

            ID3D12CommandList* ppCommandLists[] = { _commandList[swapBuffer].Get() };
            _commandQueue->ExecuteCommandLists(1, ppCommandLists);


            for (int i = 0; i < SwapChainBufferCount; ++i) {
                ThrowIfFailed(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(_frameProgressFence[i].GetAddressOf())));
            }

            // Sync fence
            ComPtr<ID3D12Fence> fence;
            ThrowIfFailed(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())));

            _commandQueue->Signal(fence.Get(), 1);

            if (fence->GetCompletedValue() < 1) 
            {
                HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                fence->SetEventOnCompletion(1, event);

                WaitForSingleObjectEx(event, INFINITE, false);
                CloseHandle(event);
            }
        }
    }

    void Render() 
    {
        using namespace DirectX;
        SceneConstantBuffer data;
        data.IndicesCount = _indicesCount;
        data.VerticesCount = _verticesCount;
        const uint8_t swapBuffer = _frameId % SwapChainBufferCount;

        XMMATRIX world = XMMATRIX(g_XMIdentityR0, g_XMIdentityR1, g_XMIdentityR2, g_XMIdentityR3);
        XMMATRIX view = XMMatrixTranslation(0, -4, -10);
        XMMATRIX proj = XMMatrixPerspectiveFovRH(XM_PI / 3.0f, static_cast<float>(_winWidth)/static_cast<float>(_winHeight), 0.1f, 100.f);

        XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&data.WorldView, XMMatrixTranspose(world * view));
        XMStoreFloat4x4(&data.WorldViewProj, XMMatrixTranspose(world * view * proj));

        memcpy(_cbvDataBegin + sizeof(SceneConstantBuffer) * _currentSwapChainBufferIndex, &data, sizeof(data) );

        ThrowIfFailed(_commandAllocator[swapBuffer]->Reset());
        ThrowIfFailed(_commandList[swapBuffer]->Reset(_commandAllocator[swapBuffer].Get(), _pipelineState.Get()));

        _commandList[swapBuffer]->SetGraphicsRootSignature(_rootSignature.Get());
        _commandList[swapBuffer]->RSSetViewports(1, &_viewport);
        _commandList[swapBuffer]->RSSetScissorRects(1, &_scissorRect);

        const auto toRenderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(_renderTargets[swapBuffer].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        _commandList[swapBuffer]->ResourceBarrier(1, &toRenderTargetBarrier);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(_rtvHeap->GetCPUDescriptorHandleForHeapStart(), swapBuffer, _rtvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        _commandList[swapBuffer]->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        _commandList[swapBuffer]->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        _commandList[swapBuffer]->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        _commandList[swapBuffer]->SetGraphicsRootConstantBufferView(0, _constantBuffer->GetGPUVirtualAddress() + sizeof(SceneConstantBuffer) * _currentSwapChainBufferIndex);

        _commandList[swapBuffer]->SetGraphicsRootShaderResourceView(1, _vertexBufferResource.Get()->GetGPUVirtualAddress());
        _commandList[swapBuffer]->SetGraphicsRootShaderResourceView(2, _indexBufferResource.Get()->GetGPUVirtualAddress());

        _commandList[swapBuffer]->DispatchMesh((_indicesCount/3 + 31)/32, 1, 1);

        const auto toPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(_renderTargets[swapBuffer].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        _commandList[swapBuffer]->ResourceBarrier(1, &toPresentBarrier);

        ThrowIfFailed(_commandList[swapBuffer]->Close());

        ID3D12CommandList* ppCommandLists[] =  { _commandList[swapBuffer].Get() };
        _commandQueue->ExecuteCommandLists(1, ppCommandLists);

        ThrowIfFailed(_swapChain->Present(1, 0));

        // We will be using single buffering actually...
        const UINT nextFence = _frameId + 1;
        ThrowIfFailed(_commandQueue->Signal(_frameProgressFence[swapBuffer].Get(), nextFence));
        _frameId++;

        if (_frameProgressFence[swapBuffer]->GetCompletedValue() < nextFence) 
        {
            HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            ThrowIfFailed(_frameProgressFence[swapBuffer]->SetEventOnCompletion(nextFence, event));

            WaitForSingleObjectEx(event, INFINITE, false);
            CloseHandle(event);
        }
    }

    int Run()
    {
        MSG msg = {0};
        while(msg.message != WM_QUIT)
        {
            if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else 
            {

                Render();
                //RenderFrame();
            }
        }
        return (int)msg.wParam;
    }

private:
    static LRESULT CALLBACK
    MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch(msg)
        {
        case WM_CREATE:
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProc( hwnd, msg, wParam, lParam );
    }
};


int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE /*hPrevInstance*/,
    LPSTR /*lpCmdLine*/,
    int /*nCmdShow*/
)
{
    if(AllocConsole()){
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    App app(hInstance);
    app.Run();
}