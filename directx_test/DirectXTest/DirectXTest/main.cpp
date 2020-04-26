#include <Windows.h>
#include <iostream>
#include <debugapi.h>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include<d3dcompiler.h>

#include <DirectXMath.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace std;
using namespace DirectX;

const unsigned int WINDOW_WIDTH = 1280;
const unsigned int WINDOW_HEIGHT = 720;

ID3D12Device* device = nullptr;
IDXGIFactory6* dxgiFactory = nullptr;
IDXGISwapChain4* swapChain = nullptr;
ID3D12CommandAllocator* commandAllocator = nullptr;
ID3D12GraphicsCommandList* commandList = nullptr;
ID3D12CommandQueue* commandQueue = nullptr;
ID3D12DescriptorHeap* rtvHeaps = nullptr;

ID3D12Fence* fence = nullptr;
UINT64 fenceValue = 0;

ID3D12Resource* vertexBuffer = nullptr;
ID3D12Resource* indexBuffer = nullptr;

XMFLOAT3 vertices[] = {
	{ -0.4f, -0.7f, 0.0f },
	{ -0.4f, 0.7f, 0.0f },
	{ 0.4f, -0.7f, 0.0f },
	{ 0.4f, 0.7f, 0.0f },
};
unsigned short indices[] = {
	0, 1, 2,
	2, 1, 3,
};

ID3D12RootSignature* rootSignature = nullptr;
ID3D12PipelineState* pipelineState = nullptr;

void DxInitialize(HWND hwnd)
{
	ID3D12Debug* debugLayer = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer)))) {
		debugLayer->EnableDebugLayer();
		debugLayer->Release();
	}

	if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgiFactory)))) {
		CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	}

	vector<IDXGIAdapter*> adapters;
	IDXGIAdapter* tempAdapter = nullptr;
	for (int i = 0; dxgiFactory->EnumAdapters(i, &tempAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
		adapters.push_back(tempAdapter);
	}

	tempAdapter = nullptr;
	for (auto adapter : adapters) {
		DXGI_ADAPTER_DESC adapterDesc = {};
		adapter->GetDesc(&adapterDesc);

		wstring str = adapterDesc.Description;
		if (str.find(L"Microsoft") != string::npos) {
			tempAdapter = adapter;
			break;
		}
	}

	D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	D3D_FEATURE_LEVEL featureLevel;
	for (auto lv : levels) {
		auto result = D3D12CreateDevice(tempAdapter, lv, IID_PPV_ARGS(&device));
		if (result == S_OK) {
			featureLevel = lv;
			break;
		}
	}

	HRESULT result = S_OK;
	result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
	commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	commandQueueDesc.NodeMask = 0;
	commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	result = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = WINDOW_WIDTH;
	swapChainDesc.Height = WINDOW_HEIGHT;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = false;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	result = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, (IDXGISwapChain1**)&swapChain);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	result = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));

	DXGI_SWAP_CHAIN_DESC tempDesc = {};
	result = swapChain->GetDesc(&tempDesc);
	vector<ID3D12Resource*> backBuffers(tempDesc.BufferCount);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
	UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (int i = 0; i < tempDesc.BufferCount; ++i) {
		result = swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
		device->CreateRenderTargetView(backBuffers[i], nullptr, handle);

		handle.ptr += stride;
	}

	result = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(vertices);
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	result = device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer));

	XMFLOAT3* vertexMap = nullptr;
	result = vertexBuffer->Map(0, nullptr, (void**)&vertexMap);
	copy(begin(vertices), end(vertices), vertexMap);
	vertexBuffer->Unmap(0, nullptr);

	resourceDesc.Width = sizeof(indices);

	result = device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer));

	unsigned short* indexMap = nullptr;
	result = indexBuffer->Map(0, nullptr, (void**)&indexMap);
	copy(begin(indices), end(indices), indexMap);
	indexBuffer->Unmap(0, nullptr);

	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;

	ID3DBlob* errorBlob = nullptr;

	result = D3DCompileFromFile(
		L"BasicVertexShader.hlsl",
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"BasicVS",
		"vs_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&vsBlob,
		&errorBlob
	);
	if (FAILED(result)) {
		std::string message;
		message.resize(errorBlob->GetBufferSize());
		copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), message.begin());
		message += "\n";
		OutputDebugStringA(message.c_str());
	}

	result = D3DCompileFromFile(
		L"BasicPixelShader.hlsl",
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"BasicPS",
		"ps_5_0",
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&psBlob,
		&errorBlob
	);
	if (FAILED(result)) {
		std::string message;
		message.resize(errorBlob->GetBufferSize());
		copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), message.begin());
		message += "\n";
		OutputDebugStringA(message.c_str());
	}

	D3D12_INPUT_ELEMENT_DESC inputLayout = {};
	inputLayout.SemanticName = "POSITION";
	inputLayout.SemanticIndex = 0;
	inputLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputLayout.InputSlot = 0;
	inputLayout.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputLayout.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputLayout.InstanceDataStepRate = 0;
	D3D12_INPUT_ELEMENT_DESC inputLayouts[] = { inputLayout };

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	ID3D10Blob* rootSigBlob = nullptr;
	result = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSigBlob, &errorBlob);
	if (FAILED(result)) {
		std::string message;
		message.resize(errorBlob->GetBufferSize());
		copy_n((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize(), message.begin());
		message += "\n";
		OutputDebugStringA(message.c_str());
	}
	result = device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	rootSigBlob->Release();
	pipelineStateDesc.pRootSignature = rootSignature;

	pipelineStateDesc.VS.pShaderBytecode = vsBlob->GetBufferPointer();
	pipelineStateDesc.VS.BytecodeLength = vsBlob->GetBufferSize();
	pipelineStateDesc.PS.pShaderBytecode = psBlob->GetBufferPointer();
	pipelineStateDesc.PS.BytecodeLength = psBlob->GetBufferSize();

	pipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	pipelineStateDesc.RasterizerState.MultisampleEnable = false;
	pipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pipelineStateDesc.RasterizerState.DepthClipEnable = true;
	pipelineStateDesc.RasterizerState.FrontCounterClockwise = false;
	pipelineStateDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	pipelineStateDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	pipelineStateDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	pipelineStateDesc.RasterizerState.AntialiasedLineEnable = false;
	pipelineStateDesc.RasterizerState.ForcedSampleCount = 0;
	pipelineStateDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	pipelineStateDesc.BlendState.AlphaToCoverageEnable = false;
	pipelineStateDesc.BlendState.IndependentBlendEnable = false;

	D3D12_RENDER_TARGET_BLEND_DESC blendDesc = {};
	blendDesc.BlendEnable = false;
	blendDesc.LogicOpEnable = false;
	blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pipelineStateDesc.BlendState.RenderTarget[0] = blendDesc;

	pipelineStateDesc.DepthStencilState.DepthEnable = false;
	pipelineStateDesc.DepthStencilState.StencilEnable = false;

	pipelineStateDesc.InputLayout.pInputElementDescs = inputLayouts;
	pipelineStateDesc.InputLayout.NumElements = _countof(inputLayouts);

	pipelineStateDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	pipelineStateDesc.NumRenderTargets = 1;
	pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	pipelineStateDesc.SampleDesc.Count = 1;
	pipelineStateDesc.SampleDesc.Quality = 0;

	result = device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&pipelineState));
}

void DxUpdate()
{
	UINT index = swapChain->GetCurrentBackBufferIndex();

	HRESULT result = S_OK;
	ID3D12Resource* backBuffer = nullptr;
	result = swapChain->GetBuffer(index, IID_PPV_ARGS(&backBuffer));

	D3D12_RESOURCE_BARRIER barrierDesc = {};
	barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrierDesc.Transition.pResource = backBuffer;
	barrierDesc.Transition.Subresource = 0;
	barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList->ResourceBarrier(1, &barrierDesc);

	commandList->SetPipelineState(pipelineState);

	commandList->SetGraphicsRootSignature(rootSignature);

	UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += index * stride;
	commandList->OMSetRenderTargets(1, &handle, true, nullptr);

	D3D12_VIEWPORT viewport = {};
	viewport.Width = WINDOW_WIDTH;
	viewport.Height = WINDOW_HEIGHT;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MaxDepth = 1.0f;
	viewport.MinDepth = 0.0f;
	commandList->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect = {};
	scissorRect.top = 0;
	scissorRect.left = 0;
	scissorRect.bottom = scissorRect.top + WINDOW_HEIGHT;
	scissorRect.right = scissorRect.left + WINDOW_WIDTH;
	commandList->RSSetScissorRects(1, &scissorRect);

	float clearColor[] = { 1.0f, 0.0f, 0.0f, 1.0f };
	commandList->ClearRenderTargetView(handle, clearColor, 0, nullptr);

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vbView.SizeInBytes = sizeof(vertices);
	vbView.StrideInBytes = sizeof(vertices[0]);
	commandList->IASetVertexBuffers(0, 1, &vbView);

	D3D12_INDEX_BUFFER_VIEW ibView = {};
	ibView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	ibView.Format = DXGI_FORMAT_R16_UINT;
	ibView.SizeInBytes = sizeof(indices);
	commandList->IASetIndexBuffer(&ibView);

	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

	barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &barrierDesc);

	commandList->Close();

	ID3D12CommandList* commandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(1, commandLists);
	
	commandQueue->Signal(fence, ++fenceValue);
	if (fence->GetCompletedValue() != fenceValue) {
		auto event = CreateEvent(nullptr, false, false, nullptr);
		fence->SetEventOnCompletion(fenceValue, event);

		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}

	commandAllocator->Reset();
	commandList->Reset(commandAllocator, nullptr);

	swapChain->Present(1, 0);
}

LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (msg == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	WNDCLASSEX w = {};
	w.cbSize = sizeof(WNDCLASSEX);
	w.lpfnWndProc = (WNDPROC)WindowProcedure;
	w.lpszClassName = TEXT("DX12Sample");
	w.hInstance = GetModuleHandle(nullptr);
	RegisterClassEx(&w);

	RECT wrc = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	HWND hwnd = CreateWindow(
		w.lpszClassName,
		TEXT("DX12ƒeƒXƒg"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		w.hInstance,
		nullptr
	);

	DxInitialize(hwnd);

	ShowWindow(hwnd, SW_SHOW);

	MSG msg = {};
	while (true) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (msg.message == WM_QUIT) {
			break;
		}

		DxUpdate();
	}

	UnregisterClass(w.lpszClassName, w.hInstance);
}