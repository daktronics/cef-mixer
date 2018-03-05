# Modifying Chromium/CEF for high-performance OSR

It took a considerable amount of time to figure out all the changes required to update both Chromium and CEF to support 
offscreen rendering directly to a shared texture.  I thought it would be best to capture everything in a document in case anyone 
(including myself) wishes to attempt something similar in the future.

# Chromium Modifications

1. Update `ui::Compositor` to allow options to use shared textures
ui::Compositor represents the interaction point between CEF and Chromium that we are going to modify to enable and retrieve shared texture information.
   
   1. in ui/compositor/compositor.h add the following declarations:
      
      1. in existing `class ContextFactoryPrivate` add :
      
         ```virtual void* GetSharedTexture(ui::Compositor* compositor) = 0;```
	 
      2. in existing `class Compositor` add:
	
	```void* GetSharedTexture();
	void EnableSharedTexture(bool enable);
	bool shared_texture_enabled() const{
	  return shared_texture_enabled_;
	}
	
	bool shared_texture_enabled_ = false;```
			


# Apply Chromium patch and modify CEF
