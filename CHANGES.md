# Modifying Chromium/CEF for high-performance OSR

It took a considerable amount of time to figure out all the changes required to update both Chromium and CEF to support 
offscreen rendering directly to a shared texture.  I thought it would be best to capture everything in a document in case anyone 
(including myself) wishes to attempt something similar in the future.

# Chromium Modifications

1. Update `ui::Compositor` to allow options to use shared textures
ui::Compositor represents the interaction point between CEF and Chromium that we are going to modify to enable and retrieve shared texture information.
   
   1. In ui/compositor/compositor.h add the following declarations:
      
      1. In existing `class ContextFactoryPrivate` add :
      
         ```virtual void* GetSharedTexture(ui::Compositor* compositor) = 0;```
	 
      2. In existing `class Compositor` add:
	
	     ```c
	     void* GetSharedTexture();
	     void EnableSharedTexture(bool enable);
	     bool shared_texture_enabled() const{
	       return shared_texture_enabled_;
 	     }	
	     bool shared_texture_enabled_ = false;
	     ```
	
   2. In ui/compositor/compositor.cc add the following implementations:
	
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
		
2.  Update OffscreenBrowserCompositorOutputSurface to use a shared texture for its FBO

	This is the main object used for accelerated off-screen rendering in Chromium.  We're going to modify it so
	it will use a shared d3d11 texture as the color attachment for its FBO.

   1. In content/browser/browser_compositor_output_surface.h add the following declaration to BrowserCompositorOuptutSurface:
       
	   ```c
	   virtual void* GetSharedTexture() const;
       ```	   
		
   2. In content/browser/browser_compositor_output_surface.cc add the following implementation:
   
      ```c
	  void* BrowserCompositorOuptutSurface::GetSharedTexture() const
	  {
	    return nullptr;
	  }
	  ```
		
   3. In content/browser/offscreen_browser_compositor_output_surface.h 
	
      1. Add the shared_texture_enabled flag to the ctor for OffscreenBrowserCompositorOutputSurface
		
		 ```c
		 OffscreenBrowserCompositorOutputSurface(
		   scoped_refptr<ui::ContextProviderCommandBuffer> context,
           const UpdateVSyncParametersCallback& update_vsync_parameters_callback,
		   std::unique_ptr<viz::CompositorOverlayCandidateValidator> overlay_candidate_validator,
		   bool shared_texture_enabled);
		```
		
      2. Add the following declaration to OffscreenBrowserCompositorOutputSurface:
	     
		 ```void* GetSharedTexture() const override;```
	
      3. Add the following members to OffscreenBrowserCompositorOutputSurface
		
		 ```c
		 bool shared_texture_enabled_ = false;
         uint64_t shared_handle_ = 0ull;
         uint32_t shared_texture_ = 0;
		 ```

   4. In content/browser/offscreen_browser_compositor_output_surface.cc 
	
      1. Add the shared_texture_enabled to the ctor:
		
         ```c
		 OffscreenBrowserCompositorOutputSurface::OffscreenBrowserCompositorOutputSurface(
					scoped_refptr<ui::ContextProviderCommandBuffer> context,
					const UpdateVSyncParametersCallback& update_vsync_parameters_callback,
					std::unique_ptr<viz::CompositorOverlayCandidateValidator>
						overlay_candidate_validator,
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
												  GL_TEXTURE_2D, color_attachment,
												  0);
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
		 
3. Update GpuProcessTransportFactory to return the shared texture in use
