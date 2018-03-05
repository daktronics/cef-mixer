# Modifying Chromium/CEF for high-performance OSR

It took a considerable amount of time to figure out all the changes required to update both Chromium and CEF to support 
offscreen rendering directly to a shared texture.  I thought it would be best to capture everything in a document in case anyone 
(including myself) wishes to attempt something similar in the future.

# Chromium Modifications

1. Update ui::Compositor to allow options to use shared textures

ui::Compositor represents the interaction point between CEF and Chromium that we are going to modify to enable and retrieve shared texture information.
   
    1. in ui/compositor/compositor.h add the following declarations:
   
		    1. in existing `class ContextFactoryPrivate` add :
	  
			  `virtual void* GetSharedTexture(ui::Compositor* compositor) = 0;`
			
		ii) in existing class Compositor:
		
			void* GetSharedTexture();
			
			void EnableSharedTexture(bool enable);
			bool shared_texture_enabled() const{
				return shared_texture_enabled_;
			}
			
			bool shared_texture_enabled_ = false;
		
	b) in ui/compositor/compositor.cc add the following implementations:
	
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



# Build and Update Chromium

Because Chromium and CEF are 2 separate projects - we're going to modify Chromium outside of the CEF build tree.  With our changes to Chromium, we will then create a patch file and integrate it with CEF.

1.  Follow [these instructions][chromium_win] to setup a development environment for Chromium
    * Do not use --no-history when performing the fetch
    * After you do perform the initial fetch, run `git fetch --tags`
    * Locate the Chromium hash that CEF master is using (this can be located in CHROMIUM_BUILD_COMPATIBILITY.txt directly in the CEF source root)
    * Run `git checkout -f <hash from CHROMIUM_BUILD_COMPATIBILITY.txt>`
    * Run `gclient sync --with_branch_heads`
    
2. Use ninja to generate and build Chromium

3. 


# Apply Chromium patch and modify CEF



[chromium_win]: https://chromium.googlesource.com/chromium/src/+/master/docs/windows_build_instructions.md
