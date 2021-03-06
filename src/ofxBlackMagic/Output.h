#pragma once

#include "Utils.h"
#include "Frame.h"

#include "DeviceList.h"
#include "ofMain.h"

namespace ofxBlackmagic {
	class Output : public IDeckLinkVideoOutputCallback
	{
	public:

		Output();
		~Output();

		bool start(const DeviceDefinition&, const BMDDisplayMode&, uint32_t numChannels = 4);
		void stop();

		void publishTexture(ofTexture &tex);
		void publishPixels(ofPixels &pix);
		
		void setEnableQueueMode(bool enable, int numbuffer = 2);
		bool isEnableQueueMode() const { return b_queue_mode; }
		void publishQueuedPixels(ofPixels &pix);
		bool isQueueEmpty() const { return queued_pixels.empty(); }
	protected:
		uint32_t numChannels;

		IDeckLinkOutput* pDLOutput;
		IDeckLinkMutableVideoFrame*	pDLVideoFrame;

		uint32_t uiFrameWidth;
		uint32_t uiFrameHeight;

		uint32_t uiFPS;
		uint32_t uiTotalFrames;

		BMDTimeValue frameDuration;
		BMDTimeScale frameTimescale;

		bool b_queue_mode;
		queue<ofPixels> queued_pixels;
		ofPixels pixels[2];
		ofPixels *front_buffer, *back_buffer;
		ofMutex mutex;
		std::mutex lock_close;
		std::condition_variable cv;
		bool has_new_frame;

		bool initDeckLink(const DeviceDefinition&);
		bool startDeckLink(BMDDisplayMode mode);

		void resetFrame();
		void setPreroll();

	public:

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) { return E_NOINTERFACE; }
		virtual ULONG STDMETHODCALLTYPE AddRef(void) { return 1; }
		virtual ULONG STDMETHODCALLTYPE Release(void) { return 1; }

		virtual HRESULT ScheduledFrameCompleted(/* in */ IDeckLinkVideoFrame *completedFrame, /* in */ BMDOutputFrameCompletionResult result);
		virtual HRESULT ScheduledPlaybackHasStopped(void) { return S_OK; }
	};

}
