#include <Windows.h>
#include <iostream>
#include <debugapi.h>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace std;

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
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
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

	UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += index * stride;
	commandList->OMSetRenderTargets(1, &handle, true, nullptr);

	float clearColor[] = { 1.0f, 0.0f, 0.0f, 1.0f };
	commandList->ClearRenderTargetView(handle, clearColor, 0, nullptr);

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