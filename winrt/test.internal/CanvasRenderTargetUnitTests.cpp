// Copyright (c) Microsoft Corporation. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use these files except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.

#include "pch.h"

#include "CanvasRenderTarget.h"
#include "TestBitmapResourceCreationAdapter.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace ABI::Windows::Foundation;

TEST_CLASS(CanvasRenderTargetTests)
{
    std::shared_ptr<CanvasRenderTargetManager> m_manager;
    ComPtr<MockD2DDevice> m_d2dDevice;
    ComPtr<StubCanvasDevice> m_canvasDevice;

public:
    TEST_METHOD_INITIALIZE(Reset)
    {
        m_manager = std::make_shared<CanvasRenderTargetManager>();
        m_d2dDevice = Make<MockD2DDevice>();
        m_canvasDevice = Make<StubCanvasDevice>(m_d2dDevice);

        // Make the D2D device return StubD2DDeviceContexts
        m_d2dDevice->MockCreateDeviceContext = 
            [&](D2D1_DEVICE_CONTEXT_OPTIONS, ID2D1DeviceContext1** value)
            {
                auto deviceContext = Make<StubD2DDeviceContext>(m_d2dDevice.Get());
                ThrowIfFailed(deviceContext.CopyTo(value));
            };
    }

    ComPtr<CanvasRenderTarget> CreateRenderTarget(
        ComPtr<ID2D1Bitmap1> bitmap = nullptr,
        Size size = Size{ 1, 1 })
    {
        if (!bitmap)
            bitmap = Make<StubD2DBitmap>();

        m_canvasDevice->MockCreateBitmap =
            [&](Size)
            {
                Assert::IsNotNull(bitmap.Get());
                auto result = bitmap;
                bitmap.Reset();
                return result;
            };

        auto renderTarget = m_manager->Create(m_canvasDevice.Get(), size);

        m_canvasDevice->MockCreateBitmap = nullptr;

        return renderTarget;
    }

    ComPtr<CanvasRenderTarget> CreateRenderTargetAsWrapper(ComPtr<ID2D1Bitmap1> bitmap)
    {
        return m_manager->GetOrCreate(bitmap.Get());
    }

    TEST_METHOD(CanvasRenderTarget_Implements_Expected_Interfaces)
    {
        auto renderTarget = CreateRenderTarget();

        ASSERT_IMPLEMENTS_INTERFACE(renderTarget, ICanvasRenderTarget);
        ASSERT_IMPLEMENTS_INTERFACE(renderTarget, ICanvasBitmap);
        ASSERT_IMPLEMENTS_INTERFACE(renderTarget, ICanvasImage);
        ASSERT_IMPLEMENTS_INTERFACE(renderTarget, ABI::Windows::Foundation::IClosable);
        ASSERT_IMPLEMENTS_INTERFACE(renderTarget, ICanvasImageInternal);
    }

    TEST_METHOD(CanvasRenderTarget_InterfacesAreTransitive)
    {
        auto renderTarget = CreateRenderTarget();

        ComPtr<ICanvasBitmap> bitmap;
        ThrowIfFailed(renderTarget.As(&bitmap));

        ComPtr<ICanvasRenderTarget> irenderTarget;
        Assert::AreEqual(S_OK, bitmap.As(&irenderTarget));
    }

    TEST_METHOD(CanvasRenderTarget_Close)
    {
        auto renderTarget = CreateRenderTarget();

        ComPtr<IClosable> renderTargetClosable;
        ThrowIfFailed(renderTarget.As(&renderTargetClosable));

        Assert::AreEqual(S_OK, renderTargetClosable->Close());

        ComPtr<ICanvasDrawingSession> drawingSession;
        Assert::AreEqual(RO_E_CLOSED, renderTarget->CreateDrawingSession(&drawingSession));
    }


    TEST_METHOD(CanvasRenderTarget_DrawingSession)
    {        
        auto d2dBitmap = Make<StubD2DBitmap>();    
        auto renderTarget = CreateRenderTarget(d2dBitmap);

        ComPtr<ICanvasDrawingSession> drawingSession;
        ThrowIfFailed(renderTarget->CreateDrawingSession(&drawingSession));

        ValidateDrawingSession(drawingSession.Get(), m_d2dDevice.Get(), d2dBitmap.Get());
    }

#if WRAPPED_RENDER_TARGETS_NOT_CURRENTLY_SUPPORTED
    TEST_METHOD(CanvasRenderTarget_Wrapped_CreatesDrawingSession)
    {
        auto d2dBitmap = Make<StubD2DBitmap>();
        auto renderTarget = CreateRenderTargetAsWrapper(d2dBitmap);

        ComPtr<ICanvasDrawingSession> drawingSession;
        ThrowIfFailed(renderTarget->CreateDrawingSession(&drawingSession));

        ValidateDrawingSession(drawingSession.Get(), m_d2dDevice.Get(), d2dBitmap.Get());
    }
#endif

    //
    // Validates the drawing session.  We do this using the underlying D2D
    // resources.
    //
    void ValidateDrawingSession(
        ICanvasDrawingSession* drawingSession,
        ID2D1Device* expectedDevice,
        ID2D1Image* expectedTarget)
    {
        //
        // Pull the ID2D1DeviceContext1 out of the drawing session
        //
        ComPtr<ICanvasResourceWrapperNative> drawingSessionResourceWrapper;
        ThrowIfFailed(drawingSession->QueryInterface(drawingSessionResourceWrapper.GetAddressOf()));

        ComPtr<IUnknown> deviceContextAsUnknown;
        ThrowIfFailed(drawingSessionResourceWrapper->GetResource(&deviceContextAsUnknown));

        ComPtr<ID2D1DeviceContext1> deviceContext;
        ThrowIfFailed(deviceContextAsUnknown.As(&deviceContext));

        //
        // Check the device
        //
        ComPtr<ID2D1Device> deviceContextDevice;
        deviceContext->GetDevice(&deviceContextDevice);

        Assert::AreEqual(expectedDevice, deviceContextDevice.Get());

        //
        // Check the currently set target
        //
        ComPtr<ID2D1Image> deviceContextTarget;
        deviceContext->GetTarget(&deviceContextTarget);

        Assert::AreEqual(expectedTarget, deviceContextTarget.Get());
    }


    TEST_METHOD(CanvasRenderTarget_InheritanceFromBitmap)
    {
        // This exercises some of the Bitmap methods on CanvasRenderTarget.

        Size expectedSize = { 33, 44 };

        auto d2dBitmap = Make<StubD2DBitmap>();
        d2dBitmap->MockGetSize =
            [&](float* width, float* height)
            {
                *width = expectedSize.Width;
                *height = expectedSize.Height;
            };                    
        d2dBitmap->MockGetPixelSize =
            [&](unsigned int* width, unsigned int* height)
            {
                *width = static_cast<UINT>(expectedSize.Width);
                *height = static_cast<UINT>(expectedSize.Height);
            };

        auto renderTarget = CreateRenderTarget(d2dBitmap, expectedSize);

        ComPtr<ICanvasBitmap> renderTargetAsBitmap;
        ThrowIfFailed(renderTarget.As(&renderTargetAsBitmap));

        Size retrievedSize;

        ThrowIfFailed(renderTargetAsBitmap->get_Size(&retrievedSize));
        Assert::AreEqual(expectedSize.Width, retrievedSize.Width);
        Assert::AreEqual(expectedSize.Height, retrievedSize.Height);

        // Bitmaps are constructed against default DPI, currently, so the pixel 
        // size and dips size should be equal.
        ThrowIfFailed(renderTargetAsBitmap->get_SizeInPixels(&retrievedSize));
        Assert::AreEqual(expectedSize.Width, retrievedSize.Width);
        Assert::AreEqual(expectedSize.Height, retrievedSize.Height);
    }
};