#include "Input.h"

namespace ofxBlackmagic {
	//---------
	Input::Input() {
		this->useTexture = true;
		this->newFrameReady = false;
		this->isFrameNewFlag = false;
		this->referenceCount = 1;
		this->state = Waiting;
		this->useDeckLinkColorConverter = true;
		this->depthPrefer = DEPTH_8BIT;
	}

	//---------
	Input::~Input() {
		this->stopCapture();
	}

	HRESULT	STDMETHODCALLTYPE Input::QueryInterface(REFIID iid, LPVOID *ppv)
	{
		HRESULT	result = E_NOINTERFACE;

		if (ppv == NULL)
			return E_INVALIDARG;

		// Initialise the return result
		*ppv = NULL;

		// Obtain the IUnknown interface and compare it the provided REFIID
		if (iid == IID_IUnknown)
		{
			*ppv = this;
			AddRef();
			result = S_OK;
		}
		else if (iid == IID_IDeckLinkInputCallback)
		{
			*ppv = (IDeckLinkInputCallback*)this;
			AddRef();
			result = S_OK;
		}
		else if (iid == IID_IDeckLinkNotificationCallback)
		{
			*ppv = (IDeckLinkNotificationCallback*)this;
			AddRef();
			result = S_OK;
		}

		return result;
	}

	//---------
	void Input::startCapture(const DeviceDefinition& device, const BMDDisplayMode& format) {
		IDeckLinkAttributes*  deckLinkAttributes = NULL;
		BOOL supportsFormatDetection = FALSE;
		BMDVideoInputFlags videoInputFlags = bmdVideoInputFlagDefault;
		try {
			this->stopCapture();
			this->device = device;

			// Check if input mode detection is supported.
			if (device.device->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes) == S_OK)
			{
				if (deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &supportsFormatDetection) != S_OK)
					supportsFormatDetection = FALSE;
				deckLinkAttributes->Release();
			}
			// Enable input video mode detection if the device supports it
			if (supportsFormatDetection == TRUE)
				videoInputFlags |= bmdVideoInputEnableFormatDetection;
			this->device = device;
			CHECK_ERRORS(device.device->QueryInterface(IID_IDeckLinkInput, (void**)&this->input), "Failed to query interface");
			CHECK_ERRORS(this->input->SetCallback(this), "Failed to set input callback");
			CHECK_ERRORS(this->input->EnableVideoInput(format, depthPrefer == DEPTH_8BIT ? bmdFormat8BitYUV : bmdFormat10BitYUV, videoInputFlags), "Failed to enable video input");
			CHECK_ERRORS(this->input->StartStreams(), "Failed to start streams");
			this->state = Running;
		} catch(std::exception& e) {
			OFXBM_ERROR << e.what();
			this->state = Waiting;
		}
	}

	//---------
	void Input::stopCapture() {
		if (this->state != Running) {
			return;
		}
		try {
			CHECK_ERRORS(this->input->StopStreams(), "Failed to stop streams"); 
		} catch (std::exception e) {
			OFXBM_ERROR << e.what();
		}
		this->state = Waiting;
	}

	//---------
	void Input::setMode(const BMDDisplayMode& displayMode) {
		if (this->state != Running) {
			OFXBM_ERROR << "Cannot setMode if the Input is not already running. Please use startCapture first";
			return;
		}
		this->startCapture(this->device, displayMode);
	}

	//---------
	bool Input::isFrameNew() const {
		return this->isFrameNewFlag;
	}
	
	//---------
	DeviceDefinition& Input::getDevice() {
		return this->device;
	}

	//---------
	Frame & Input::getFrame() {
		return this->videoFrame;
	}

	//---------
	void Input::setUseDeckLinkColorConverter(bool b)
	{
		this->useDeckLinkColorConverter = b;
	}

	//---------
	bool Input::isUseDeckLinkColorConverter() const
	{
		return useDeckLinkColorConverter;
	}

	float Input::getCaptureFps() const
	{
		return captureFps.getFps();
	}

	//---------
#if defined(_WIN32)
	HRESULT STDMETHODCALLTYPE Input::VideoInputFormatChanged(/* in */ BMDVideoInputFormatChangedEvents notificationEvents, /* in */ IDeckLinkDisplayMode *newMode, /* in */ BMDDetectedVideoInputFormatFlags detectedSignalFlags) {
				bool shouldRestartCaptureWithNewVideoMode = true;

				BMDPixelFormat	pixelFormat = depthPrefer == DEPTH_8BIT ? bmdFormat8BitYUV : bmdFormat10BitYUV;

				if (detectedSignalFlags & bmdDetectedVideoInputRGB444) {
					pixelFormat = depthPrefer == DEPTH_8BIT ? bmdFormat8BitARGB : bmdFormat10BitRGB;
				}

				// Restart capture with the new video mode if told to
				if (shouldRestartCaptureWithNewVideoMode) {
					// Stop the capture
					input->StopStreams();

					// Set the video input mode
					if (input->EnableVideoInput(newMode->GetDisplayMode(), pixelFormat, bmdVideoInputEnableFormatDetection) != S_OK) {
						ofLogError("This application was unable to select the new video mode.");
						goto bail;
					}

					// Start the capture
					if (input->StartStreams() != S_OK) {
						ofLogError("This application was unable to start the capture on the selected device.");
						goto bail;
					}

				}

			bail:
				return S_OK;
	}
#elif defined(__APPLE_CC__)
	HRESULT STDMETHODCALLTYPE Input::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode *newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags) {
		return S_OK;
	}
#endif
	
	//---------
	HRESULT Input::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioFrame) {
		if (videoFrame == NULL) {
			return S_OK;
		}

		this->videoFrameInput.copyFromFrame(videoFrame, useDeckLinkColorConverter);

		this->videoFrameBack.lock.lock();
		this->videoFrameBack.swapFrame(this->videoFrameInput);
		this->videoFrameBack.lock.unlock();
		this->newFrameReady = true;
		this->captureFps.newFrame();
		return S_OK;
	}

	//---------
	void Input::update() {
		this->isFrameNewFlag = this->newFrameReady;
		this->newFrameReady = false;
		if (this->isFrameNewFlag) {
			this->videoFrameBack.lock.lock();
			this->videoFrameBack.swapFrame(this->videoFrame);
			this->videoFrameBack.lock.unlock();

			if (this->isUsingTexture()) {
				auto glInternalMode = (!this->useDeckLinkColorConverter && this->videoFrame.getNumChannels() == 2) ? GL_RG : GL_RGBA;
				if (this->videoFrame.getWidth() != this->texture.getWidth() 
					|| this->videoFrame.getHeight() != this->texture.getHeight()) {
					this->texture.allocate(this->videoFrame.getWidth(), this->videoFrame.getHeight(), glInternalMode);
				}
				this->texture.loadData(this->videoFrame.getPixels(), glInternalMode);
			}
		}
	}

	//---------
	void Input::draw(float x, float y) const {
		this->draw(x, y, this->getWidth(), this->getHeight());
	}

	//---------
	void Input::draw(float x, float y, float w, float h) const {
		this->getTexture().draw(x, y, w, h);
	}
	
	//---------
	float Input::getWidth() const {
		return this->videoFrame.getWidth();
	}

	//---------
	float Input::getHeight() const {
		return this->videoFrame.getHeight();
	}

	//---------
	ofPixels & Input::getPixels() {
		return this->videoFrame.getPixels();
	}

	//---------
	const ofPixels & Input::getPixels() const {
		return this->videoFrame.getPixels();
	}

	//---------
	ofTexture & Input::getTexture() {
		return this->texture;
	}

	//---------
	const ofTexture & Input::getTexture() const {
		return this->texture;
	}

	//---------
	void Input::setUseTexture(bool useTexture) {
		this->useTexture = useTexture;
	}

	//---------
	bool Input::isUsingTexture() const {
		return this->useTexture;
	}
}