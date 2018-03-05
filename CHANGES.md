# Modifying Chromium/CEF for high-performance OSR

It took a considerable amount of time to figure out all the changes required to update both Chromium and CEF to support 
offscreen rendering directly to a shared texture.  I thought it would be best to capture everything in a document in case anyone 
(including myself) wishes to attempt something similar in the future.

## Chromium Modifications

### 1. Update `ui::Compositor` to allow options for shared textures

   ui::Compositor represents the interaction point between CEF and Chromium that we are going to modify to enable and retrieve shared texture information.
   
   1. In **ui/compositor/compositor.h** add the following declarations:
      
      1. In existing `class ContextFactoryPrivate` add :
      
         ```c
		 virtual void* GetSharedTexture(ui::Compositor* compositor) = 0;
		 ```
	 
      2. In existing `class Compositor` add:
	
	     ```c
	     void* GetSharedTexture();
	     void EnableSharedTexture(bool enable);
	     bool shared_texture_enabled() const{
	       return shared_texture_enabled_;
 	     }	
	     bool shared_texture_enabled_ = false;
	     ```
	
   2. In **ui/compositor/compositor.cc** add the following implementations:
	
		 ```c
		 void* Compositor::GetSharedTexture()
		 {
		    if (context_factory_private_)
               return context_factory_private_->GetSharedTexture(this);
            return nullptr;
         }
		
		 void Compositor::EnableSharedTexture(bool enable)
		 {
            shared_texture_enabled_ = enable;
		 }
         ```
		
### 2. Update OffscreenBrowserCompositorOutputSurface to use a shared texture for its FBO

   This is the main object used for accelerated off-screen rendering in Chromium.  We're going to modify it so
   it will use a shared d3d11 texture as the color attachment for its FBO.

   1. In **content/browser/browser_compositor_output_surface.h** add the following declaration to BrowserCompositorOuptutSurface:
       
	   ```c
	   virtual void* GetSharedTexture() const;
       ```	   
		
   2. In **content/browser/browser_compositor_output_surface.cc** add the following implementation:
   
       ```c
	   void* BrowserCompositorOuptutSurface::GetSharedTexture() const
	   {
	      return nullptr;
	   }
	   ```
		
   3. In **content/browser/offscreen_browser_compositor_output_surface.h**
	
      1. Add the shared_texture_enabled flag to the ctor for OffscreenBrowserCompositorOutputSurface
		
		 ```c
		 OffscreenBrowserCompositorOutputSurface(
		   scoped_refptr<ui::ContextProviderCommandBuffer> context,
           const UpdateVSyncParametersCallback& update_vsync_parameters_callback,
		   std::unique_ptr<viz::CompositorOverlayCandidateValidator> overlay_candidate_validator,
		   bool shared_texture_enabled);
		 ```
		
      2. Add the following declaration to OffscreenBrowserCompositorOutputSurface:
	     
		 ```c
		 void* GetSharedTexture() const override;
		 ```
	
      3. Add the following members to OffscreenBrowserCompositorOutputSurface
		
		 ```c
		 bool shared_texture_enabled_ = false;
         uint64_t shared_handle_ = 0ull;
         uint32_t shared_texture_ = 0;
		 ```

   4. In **content/browser/offscreen_browser_compositor_output_surface.cc** 
	
      1. Add the shared_texture_enabled to the ctor:
		
         ```c
		 OffscreenBrowserCompositorOutputSurface::OffscreenBrowserCompositorOutputSurface(
		        scoped_refptr<ui::ContextProviderCommandBuffer> context,
				const UpdateVSyncParametersCallback& update_vsync_parameters_callback,
				std::unique_ptr<viz::CompositorOverlayCandidateValidator> overlay_candidate_validator,
                bool shared_texture_enabled)
			: BrowserCompositorOutputSurface(std::move(context),
				update_vsync_parameters_callback,
				std::move(overlay_candidate_validator)),
				shared_texture_enabled_(shared_texture_enabled),
				weak_ptr_factory_(this) {
			capabilities_.uses_default_gl_framebuffer = false;
		 }
		 ```
			
      2. Add the implementation of GetSharedTexture:
		
	     ```c
		 void* OffscreenBrowserCompositorOutputSurface::GetSharedTexture() const
		 {
		    return (void*)shared_handle_;
         }
		 ```
			
      3. Modify EnsureBackBuffer to create a shared texture for the FBO if it was enabled
		
         ```c
		 void OffscreenBrowserCompositorOutputSurface::EnsureBackbuffer() {
  
		    GLES2Interface* gl = context_provider_->ContextGL();

			const int max_texture_size =
				context_provider_->ContextCapabilities().max_texture_size;
			int texture_width = std::min(max_texture_size, reshape_size_.width());
			int texture_height = std::min(max_texture_size, reshape_size_.height());

			GLuint color_attachment = 0;

			if (shared_texture_enabled_)
			{
				if (!shared_handle_) {
					gl->GenTextures(1, &shared_texture_);
					gl->BindTexture(GL_TEXTURE_2D, shared_texture_);
					gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					shared_handle_ = gl->CreateSharedTexture(
						shared_texture_, texture_width, texture_height, GL_TRUE);
					
					color_attachment = shared_texture_;
				}	
			}
			else
			{
				bool update_source_texture = !reflector_texture_ || reflector_changed_;
				reflector_changed_ = false;
				if (!reflector_texture_) {
					reflector_texture_.reset(new ReflectorTexture(context_provider()));

					gl->BindTexture(GL_TEXTURE_2D, reflector_texture_->texture_id());
					gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					gl->TexImage2D(GL_TEXTURE_2D, 0, GLInternalFormat(kFboTextureFormat),
						texture_width, texture_height, 0,
						GLDataFormat(kFboTextureFormat),
						GLDataType(kFboTextureFormat), nullptr);

					color_attachment = reflector_texture_->texture_id();

					// The reflector may be created later or detached and re-attached,
					// so don't assume it always exists. For example, ChromeOS always
					// creates a reflector asynchronosly when creating this for software
					// mirroring.  See |DisplayManager::CreateMirrorWindowAsyncIfAny|.
					if (reflector_ && update_source_texture)
						reflector_->OnSourceTextureMailboxUpdated(reflector_texture_->mailbox());
				}
			}

			if (color_attachment)
			{
				if (!fbo_)
					gl->GenFramebuffers(1, &fbo_);

				 gl->BindFramebuffer(GL_FRAMEBUFFER, fbo_);
				 gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                     GL_TEXTURE_2D, color_attachment, 0);
		    }
		 }
		 ```
		
      4. Modify DiscardBackBuffer to tear-down the optional shared texture
	  
         ```c
		 void OffscreenBrowserCompositorOutputSurface::DiscardBackbuffer() {
		   GLES2Interface* gl = context_provider_->ContextGL();

		   if (reflector_texture_) {
			 reflector_texture_.reset();
			 if (reflector_)
			   reflector_->OnSourceTextureMailboxUpdated(nullptr);
		   }

		   if (shared_handle_) {
			  gl->DeleteSharedTexture(shared_handle_);
			  shared_handle_ = 0ull;
           }
		   
		   if (shared_texture_) {
			  gl->DeleteTextures(1, &shared_texture_);
			  shared_texture_ = 0;
		   }

		   if (fbo_) {
			gl->BindFramebuffer(GL_FRAMEBUFFER, fbo_);
			gl->DeleteFramebuffers(1, &fbo_);
			fbo_ = 0;
		   }
		 }
		 ```
			
      5. Modify BindFrameBuffer to optionally lock our shared texture
	  
		 This method will be called before rendering a frame begins, so we issue a call to lock the shared surface
	  
		 ```c
		 void OffscreenBrowserCompositorOutputSurface::BindFramebuffer() {
  
			GLES2Interface* gl = context_provider_->ContextGL();
			
			bool need_to_bind = !!reflector_texture_.get();
			EnsureBackbuffer();
			DCHECK(fbo_);

			if (shared_handle_)
			{
				gl->BindFramebuffer(GL_FRAMEBUFFER, fbo_);
				gl->LockSharedTexture(shared_handle_, 0);
			}
			else
			{		
				DCHECK(reflector_texture_.get());
				if (need_to_bind) {
					gl->BindFramebuffer(GL_FRAMEBUFFER, fbo_);
				}	
			}
		 }
		 ```
			
      6. Modify swapbuffers to unlock our shared texture and issue a glFlush
		
		 This method will be called after a frame is rendered - unlock and Flush
		
		 ```c
		 void OffscreenBrowserCompositorOutputSurface::SwapBuffers(
			viz::OutputSurfaceFrame frame) {
		  gfx::Size surface_size = frame.size;
		  DCHECK(surface_size == reshape_size_);

		  if (reflector_) {
			if (frame.sub_buffer_rect)
			  reflector_->OnSourcePostSubBuffer(*frame.sub_buffer_rect, surface_size);
			else
			  reflector_->OnSourceSwapBuffers(surface_size);
		  }

		  // TODO(oshima): sync with the reflector's SwapBuffersComplete
		  // (crbug.com/520567).
		  // The original implementation had a flickering issue (crbug.com/515332).
		  gpu::gles2::GLES2Interface* gl = context_provider_->ContextGL();
		  
		  // if using a shared texture, we need to Flush
		  if (shared_handle_)
		  {
			  gl->UnlockSharedTexture(shared_handle_, 0);
			  gl->Flush();
		  }

		  gpu::SyncToken sync_token;
		  gl->GenUnverifiedSyncTokenCHROMIUM(sync_token.GetData());
		  context_provider_->ContextSupport()->SignalSyncToken(
			  sync_token,
			  base::Bind(
				  &OffscreenBrowserCompositorOutputSurface::OnSwapBuffersComplete,
				  weak_ptr_factory_.GetWeakPtr(), frame.latency_info, ++swap_id_));
	     }
		 ```		
		 
### 3. Update GpuProcessTransportFactory to return the shared texture in use

   1. In **content/browser/compositor/gpu_process_transport_factory.h** add the following declaration:
	
      1. In existing GpuProcessTransportFactory:
		
         ```c
		 void* GetSharedTexture(ui::Compositor* compositor) override;
		 ```
			
   2. In **content/browser/compositor/gpu_process_transport_factory.cc** add the following:
	
      1. Add the implementation for GetSharedTexture
		
		 ```c
         void* GpuProcessTransportFactory::GetSharedTexture(ui::Compositor* compositor)
		 {
		    PerCompositorDataMap::iterator it = per_compositor_data_.find(compositor);
			if (it == per_compositor_data_.end())
			   return nullptr;
			PerCompositorData* data = it->second.get();
			DCHECK(data);

			if (data->display_output_surface)
				return data->display_output_surface->GetSharedTexture();
			return nullptr;
		 }
			
	  2. In the existing method EstablishedGpuChannel, locate where the OffscreenBrowserCompositorOutputSurface is created 
		    and modify to include the shared texture flag:
		
         ```c		
		 if (data->surface_handle == gpu::kNullSurfaceHandle) {
		    display_output_surface = 
			  std::make_unique<OffscreenBrowserCompositorOutputSurface>(
			  context_provider, vsync_callback,
			  std::unique_ptr<viz::CompositorOverlayCandidateValidator>(),
			  compositor->shared_texture_enabled());
		 }
		 ```
			
   3. In **content/browser/compositor/viz_process_transport_factory.h** add the following to VizProcessTransportFactory:
	
	  ```c
	  void* GetSharedTexture(ui::Compositor*) override;
	  ```
		
   4. In **content/browser/compositor/viz_process_transport_factory.cc** add the following implementation:
	
	  ```c
	  void* VizProcessTransportFactory::GetSharedTexture(ui::Compositor*) {
	     return nullptr;	
	  }
	  
### 4. Add new gl commands to create/delete/lock/unlock shared textures

   1. In **gpu/command_buffer/cmd_buffer_functions.txt** add the following declarations
   
      ```c
      // shared handle extensions
	  GL_APICALL GLuint64     GL_APIENTRY glCreateSharedTexture (GLuint texture_id, GLsizei width, GLsizei height, GLboolean syncable);
	  GL_APICALL void         GL_APIENTRY glLockSharedTexture (GLuint64 shared_handle, GLuint64 sync_key);
	  GL_APICALL void         GL_APIENTRY glUnlockSharedTexture (GLuint64 shared_handle, GLuint64 sync_key);
	  GL_APICALL void         GL_APIENTRY glDeleteSharedTexture (GLuint64 shared_handle);
	  ```
		
   2. In **gpu/command_buffer/build_gles2_cmd_buffer.py** add the following to represent our 4 new commands:
	
	  ```c
		'CreateSharedTexture': {
			'type': 'Custom',
			'data_transfer_methods': ['shm'],
			'cmd_args': 'GLint texture_id, GLint width, '
						'GLint height, GLboolean syncable, GLuint64* result',
			'result': ['GLuint64'],
			'unit_test': False,
			'impl_func': False,
			'client_test': False,
			'extension': True,
		},
		'LockSharedTexture': {
			'type': 'Custom',
			'unit_test': False,
			'impl_func': False,
			'client_test': False,
			'extension': True,
		},
		'UnlockSharedTexture': {
			'type': 'Custom',
			'unit_test': False,
			'impl_func': False,
			'client_test': False,
			'extension': True,
		},
		'DeleteSharedTexture': {
			'type': 'Custom',
			'unit_test': False,
			'impl_func': False,
			'client_test': False,
			'extension': True,
		}
	  ```
		
   3. From a command prompt run: (current dir should be <chromium>/src/) to generate code for new commands
	
	  ```Batchfile
	  > python gpu/command_buffer/build_gles2_cmd_buffer.py
	  ```
		
   4. In **gpu/command_buffer/client/gles2_implementation.cc** add the following implementations:
	
      ```c
	  GLuint64 GLES2Implementation::CreateSharedTexture(
			GLuint texture_id, GLsizei width, GLsizei height, GLboolean lockable) 
	  {
		 typedef cmds::CreateSharedTexture::Result Result;
		 Result* result = GetResultAs<Result*>();
		 if (!result) {
			return 0;
		 }
		 *result = 0;
		 helper_->CreateSharedTexture(texture_id, width, height, lockable,
			GetResultShmId(), GetResultShmOffset());
		
		 WaitForCmd();
		 return *result;
	  }

	  void GLES2Implementation::LockSharedTexture(GLuint64 shared_handle, GLuint64 key) {
		 helper_->LockSharedTexture(shared_handle, key);
	  }

	  void GLES2Implementation::UnlockSharedTexture(GLuint64 shared_handle, GLuint64 key) {
		 helper_->UnlockSharedTexture(shared_handle, key);
	  }

	  void GLES2Implementation::DeleteSharedTexture(GLuint64 shared_handle) {
		 helper_->DeleteSharedTexture(shared_handle);
	  }
	  ```
		
   5. In **gpu/command_buffer/service/gles2_cmd_decoder_passthrough.cc** add the following implementations:

      ```c  
	  error::Error GLES2DecoderPassthroughImpl::HandleCreateSharedTexture(
		 uint32_t immediate_data_size,
		 const volatile void* cmd_data) {
		 return error::kNoError;
	  }

	  error::Error GLES2DecoderPassthroughImpl::HandleLockSharedTexture(
		uint32_t immediate_data_size,
		const volatile void* cmd_data) {
		return error::kNoError;
	  }

	  error::Error GLES2DecoderPassthroughImpl::HandleUnlockSharedTexture(
		uint32_t immediate_data_size,
		const volatile void* cmd_data) {
		return error::kNoError;
	  }

	  error::Error GLES2DecoderPassthroughImpl::HandleDeleteSharedTexture(
		uint32_t immediate_data_size,
		const volatile void* cmd_data) {
		return error::kNoError;
	  }
      ```
	
   6. In **gpu/command_buffer/service/gles2_cmd_decoder.cc** add the following implementations
	
	  ```c
	  error::Error GLES2DecoderImpl::HandleCreateSharedTexture(
		 uint32_t immediate_data_size,
		 const volatile void* cmd_data) {
		 const volatile gles2::cmds::CreateSharedTexture& c =
			*static_cast<const volatile gles2::cmds::CreateSharedTexture*>(
			cmd_data);
		 GLuint texture_id = c.texture_id;
		 uint32_t width = c.width;
		 uint32_t height = c.height;
		 bool syncable = c.syncable;
		
		 typedef cmds::CreateSharedTexture::Result Result;
		 Result* result_dst = GetSharedMemoryAs<Result*>(
			c.result_shm_id, c.result_shm_offset, sizeof(*result_dst));
		 if (!result_dst) {
			return error::kOutOfBounds;
		 }
		
		 void* shared_handle = external_texture_manager()->CreateTexture(
			texture_id, width, height, syncable, texture_manager());
			
		 *result_dst = (GLuint64)(shared_handle);		
		
		 return error::kNoError;
	  }

      error::Error GLES2DecoderImpl::HandleLockSharedTexture(
			uint32_t immediate_data_size,
			const volatile void* cmd_data) {
			const volatile gles2::cmds::LockSharedTexture& c =
				*static_cast<const volatile gles2::cmds::LockSharedTexture*>(
					cmd_data);
		void* handle = (void*)(c.shared_handle());
		GLuint64 key = c.sync_key();

		external_texture_manager()->LockTexture(handle, key);

		return error::kNoError;
	  }

	  error::Error GLES2DecoderImpl::HandleUnlockSharedTexture(
		uint32_t immediate_data_size,
		const volatile void* cmd_data) {
		const volatile gles2::cmds::UnlockSharedTexture& c =
			*static_cast<const volatile gles2::cmds::UnlockSharedTexture*>(
				cmd_data);
		void* handle = (void*)(c.shared_handle());
		GLuint64 key = c.sync_key();

		external_texture_manager()->UnlockTexture(handle, key);

		return error::kNoError;
	  }

	  error::Error GLES2DecoderImpl::HandleDeleteSharedTexture(
			uint32_t immediate_data_size,
			const volatile void* cmd_data) {
			const volatile gles2::cmds::DeleteSharedTexture& c =
					*static_cast<const volatile gles2::cmds::DeleteSharedTexture*>(
					cmd_data);
		void* handle = (void*)(c.shared_handle());
			
		external_texture_manager()->DeleteTexture(handle, texture_manager());
			
		return error::kNoError;
	  }	
	
	
   7. In **gpu/command_buffer/service/gles2_cmd_decoder.cc** declare a member in GLES2DecoderImpl for ExternalTextureManager:
	
	  ```c
	  #include "gpu/command_buffer/service/external_texture_manager.h"
		
	  ...
	
	
	  ExternalTextureManager* external_texture_manager() {
		 if (!external_texture_manager_.get()) {
			external_texture_manager_.reset(new gles2::ExternalTextureManager());
		 }
		 return external_texture_manager_.get();
	  }

	  ... 
		  
	  std::unique_ptr<ExternalTextureManager> external_texture_manager_;
	  ```

### 5. Add source files for ExternalTextureManager to the build

   1. In **gpu/command_buffer/service/BUILD.gn** add: 
	
	 ```
	 "external_texture_manager.cc",
	 "external_texture_manager.h",
	 ```
		
	 to the list of sources under the target gles2_sources

	
### 6. Should be able to build the project at this point to make sure everything compiles.
	 
	 
## CEF Modifications

### 1. In **include/cef_render_handler.h** add the following new declarations:
   
   ```c
   ///
   // Called when an view has been rendered to the given shared texture handle.
   // Currently, the shared handle represents a D3D11 Texture2D that can be
   // accessed with the OpenSharedResource method available from a ID3D11Device
   ///
   virtual void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                                   PaintElementType type,
                                  const RectList& dirtyRects,
                                  void* shared_handle,
                                  uint64 sync_key) {}

   ///
   // Called when CEF is to determine if an application can support the
   // OnAcceleratedPaint method
   ///
   virtual bool CanUseAcceleratedPaint(CefRefPtr<CefBrowser> browser) {
      return false;
   }
   ```
 
### 2. In **libcef_dll/ctocpp/render_handler_ctocpp.h** add the following declarations:

   ```c
   void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
           PaintElementType type,
                          const RectList& dirtyRects,
                          void* share_handle,
                          uint64 sync_key) override;
   bool CanUseAcceleratedPaint(CefRefPtr<CefBrowser> browser) override;
   ```
   
### 3. In **libcef_dll/ctocpp/render_handler_ctocpp.cc** add the following implementations:

   ```c
   void CefRenderHandlerCToCpp::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                                                PaintElementType type,
                                                const RectList& dirtyRects,
                                                void* share_handle,
                                                uint64 sync_key) {
      cef_render_handler_t* _struct = GetStruct();
      if (CEF_MEMBER_MISSING(_struct, on_accelerated_paint))
         return;

      // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

      // Verify param: browser; type: refptr_diff
      DCHECK(browser.get());
      if (!browser.get())
         return;
   
      // Verify param: buffer; type: simple_byaddr
      DCHECK(share_handle);
      if (!share_handle)
         return;

      // Translate param: dirtyRects; type: simple_vec_byref_const
      const size_t dirtyRectsCount = dirtyRects.size();
      cef_rect_t* dirtyRectsList = NULL;
      if (dirtyRectsCount > 0) {
         dirtyRectsList = new cef_rect_t[dirtyRectsCount];
         DCHECK(dirtyRectsList);
         if (dirtyRectsList) {
            for (size_t i = 0; i < dirtyRectsCount; ++i) {
               dirtyRectsList[i] = dirtyRects[i];
            }
         }
      }

      // Execute
      _struct->on_accelerated_paint(_struct, CefBrowserCppToC::Wrap(browser), type,
                                dirtyRectsCount, dirtyRectsList, share_handle,
                                sync_key);

      // Restore param:dirtyRects; type: simple_vec_byref_const
      if (dirtyRectsList)
         delete[] dirtyRectsList;
   }

   bool CefRenderHandlerCToCpp::CanUseAcceleratedPaint(
        CefRefPtr<CefBrowser> browser) {
      cef_render_handler_t* _struct = GetStruct();
      if (CEF_MEMBER_MISSING(_struct, can_use_accelerated_paint))
         return false;

      // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

      // Verify param: browser; type: refptr_diff
      DCHECK(browser.get());
      if (!browser.get())
         return false;

      // Execute
      int _retval = _struct->can_use_accelerated_paint(
        _struct, CefBrowserCppToC::Wrap(browser));

      // Return type: bool
      return _retval ? true : false;
   }
   ```
   
### 4. In **libcef_dll/cpptoc/render_handler_cpptoc.cc** add the following implementation

   ```c
   void CEF_CALLBACK
         render_handler_on_accelerated_paint(struct _cef_render_handler_t* self,
                                    cef_browser_t* browser,
                                    cef_paint_element_type_t type,
                                    size_t dirtyRectsCount,
                                    cef_rect_t const* dirtyRects,
                                    void* share_handle,
                                    uint64 sync_key) {
      // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

      DCHECK(self);
      if (!self)
         return;
      // Verify param: browser; type: refptr_diff
      DCHECK(browser);
      if (!browser)
         return;
      // Verify param: dirtyRects; type: simple_vec_byref_const
      DCHECK(dirtyRectsCount == 0 || dirtyRects);
      if (dirtyRectsCount > 0 && !dirtyRects)
         return;
      // Verify param: buffer; type: simple_byaddr
      DCHECK(share_handle);
      if (!share_handle)
         return;

      // Translate param: dirtyRects; type: simple_vec_byref_const
      std::vector<CefRect> dirtyRectsList;
      if (dirtyRectsCount > 0) {
         for (size_t i = 0; i < dirtyRectsCount; ++i) {
            CefRect dirtyRectsVal = dirtyRects[i];
            dirtyRectsList.push_back(dirtyRectsVal);
         }
      }

      // Execute
      CefRenderHandlerCppToC::Get(self)->OnAcceleratedPaint(
        CefBrowserCToCpp::Wrap(browser), type, dirtyRectsList, share_handle,
        sync_key);
   }

   int CEF_CALLBACK
       render_handler_can_use_accelerated_paint(struct _cef_render_handler_t* self,
                                         cef_browser_t* browser) {
      // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

      DCHECK(self);
      if (!self)
         return 0;
      // Verify param: browser; type: refptr_diff
      DCHECK(browser);
      if (!browser)
         return 0;

      // Execute
      bool _retval = CefRenderHandlerCppToC::Get(self)->CanUseAcceleratedPaint(
         CefBrowserCToCpp::Wrap(browser));

      // Return type: bool
      return _retval ? 1 : 0;
   }
   ```  
### 5. In **libcef_dll/cpptoc/render_handler_cpptoc.cc** initialize the C function pointers

   ```c
   GetStruct()->on_accelerated_paint = render_handler_on_accelerated_paint;
   GetStruct()->can_use_accelerated_paint =
      render_handler_can_use_accelerated_paint;
   ```
   
### 6. Modify the OSR widget view to enabled shared textures with Chromium in **libcef/browser/osr/render_widget_host_view_osr.cc**

   1. Before the instance of ui::Compositor is created, add the following:
   
      ```c
	  // ask the application if it can handle OnAcceleratedPaint
	  bool accelerated_paint = false;
	  {
	     CefRefPtr<CefRenderHandler> handler =
		    browser_impl_->GetClient()->GetRenderHandler();
		 if (handler.get())
		    accelerated_paint = handler->CanUseAcceleratedPaint(browser_impl_.get());
      }
	  ```
	  
   2. Locate the call to `SetAcceleratedWidget(compositor_widget_)` and replace with the following:
   
      ```c
	  if (accelerated_paint) {
	     compositor_->SetAcceleratedWidget(gfx::kNullAcceleratedWidget);
	  } else {
	     compositor_->SetAcceleratedWidget(compositor_widget_);
      }
	  compositor_->EnableSharedTexture(accelerated_paint);
	  ```
	  
	  Telling Chromium to use `kNullAcceleratedWidget` will force it to use its off-screen FBO.
	  
   3. In SubmitCompositorFrame locate where `copy_frame_generator_` is instantiated and replace with the following.
   
	  We want to skip `copy_frame_generator_->GenerateCopyFrame` if we have a shared texture.
    
      ```c
	  CefRefPtr<CefRenderHandler> handler =
        browser_impl_->GetClient()->GetRenderHandler();
	  if (handler.get()) {
	     void* shared_texture = nullptr;
		 ui::Compositor* compositor = GetCompositor();
         if (compositor) {
           shared_texture = compositor->GetSharedTexture();
         }
 
         // Determine the damage rectangle for the current frame. This is the
         // same calculation that SwapDelegatedFrame uses.
         viz::RenderPass* root_pass = frame.render_pass_list.back().get();
         gfx::Size frame_size = root_pass->output_rect.size();
         gfx::Rect damage_rect =
            gfx::ToEnclosingRect(gfx::RectF(root_pass->damage_rect));
         damage_rect.Intersect(gfx::Rect(frame_size));

		 // We would normally call BrowserCompositorMac::SubmitCompositorFrame on
         // macOS, however it contains compositor resize logic that we don't
         // want. Consequently we instead call the SwapDelegatedFrame method
         // directly.
         GetDelegatedFrameHost()->SubmitCompositorFrame(
             local_surface_id, std::move(frame),
             std::move(hit_test_region_list));

         if (shared_texture) {
            CefRenderHandler::RectList rcList;
            rcList.push_back(CefRect(damage_rect.x(), damage_rect.y(),
                                   damage_rect.width(), damage_rect.height()));

            // for now just pass 0 for the sync key since we know that is what
            // Chromium is using
            handler->OnAcceleratedPaint(browser_impl_.get(),
                                      IsPopupWidget() ? PET_POPUP : PET_VIEW,
                                      rcList, shared_texture, 0);
         } else {
            if (!copy_frame_generator_.get()) {
               copy_frame_generator_.reset(
                  new CefCopyFrameGenerator(frame_rate_threshold_us_, this));
            }

            // Request a copy of the last compositor frame which will eventually
            // call OnPaint asynchronously.
            copy_frame_generator_->GenerateCopyFrame(damage_rect);
         }
      }
	  ```

### 7. We can now re-build with the CEF changes

### 8. Create a distribution for integration into an application

   A useful command for partial distributions (e.g. if you just have a Debug build) :
    
   ```Batchfile
   src/cef/tools/make_distrib.bat --x64-build --ninja-build --no-archive --allow-partial --minimal
   ```
	
   
   
    



   
