#include "Output.h"

namespace ofxBlackmagic {

	Output::Output() : pDLOutput(NULL), pDLVideoFrame(NULL), has_new_frame(false), b_queue_mode(false)
	{
	}

	Output::~Output()
	{
		stop();
	}

	bool Output::start(const DeviceDefinition & device, const BMDDisplayMode & mode)
	{
		stop();

		return initDeckLink(device) && startDeckLink(mode);
	}

	void Output::stop()
	{
		if (pDLOutput != NULL)
		{
			unique_lock<std::mutex> lock(lock_close);
			cv.wait(lock);
			pDLOutput->Release();
			pDLOutput = NULL;
		}
	}

	bool Output::initDeckLink(const DeviceDefinition& device)
	{
		bool bSuccess = FALSE;

		if (device.device->QueryInterface(IID_IDeckLinkOutput, (void**)&pDLOutput) != S_OK)
			goto error;

		if (pDLOutput->SetScheduledFrameCompletionCallback(this) != S_OK)
			goto error;

		bSuccess = TRUE;

	error:

		if (!bSuccess)
		{
			if (pDLOutput != NULL)
			{
				pDLOutput->Release();
				pDLOutput = NULL;
			}
		}

		return bSuccess;
	}

	bool Output::startDeckLink(BMDDisplayMode mode)
	{
		IDeckLinkDisplayModeIterator* pDLDisplayModeIterator = NULL;
		IDeckLinkDisplayMode* pDLDisplayMode = NULL;

		if (pDLOutput->GetDisplayModeIterator(&pDLDisplayModeIterator) == S_OK)
		{
			while (pDLDisplayModeIterator->Next(&pDLDisplayMode) == S_OK)
			{
				if (pDLDisplayMode->GetDisplayMode() == mode)
				{
					break;
				}
			}

			pDLDisplayModeIterator->Release();
		}

		if (!pDLDisplayMode)
		{
			ofLogError("ofxDeckLinkAPI::Output") << "invalid display mode";
			return false;
		}

		uiFrameWidth = pDLDisplayMode->GetWidth();
		uiFrameHeight = pDLDisplayMode->GetHeight();

		pixels[0].allocate(uiFrameWidth, uiFrameHeight, 4);
		pixels[1].allocate(uiFrameWidth, uiFrameHeight, 4);

		front_buffer = &pixels[0];
		back_buffer = &pixels[1];

		pDLDisplayMode->GetFrameRate(&frameDuration, &frameTimescale);

		uiFPS = ((frameTimescale + (frameDuration - 1)) / frameDuration);

		if (pDLOutput->EnableVideoOutput(pDLDisplayMode->GetDisplayMode(), bmdVideoOutputFlagDefault) != S_OK)
			return false;

		if (pDLOutput->CreateVideoFrame(uiFrameWidth, uiFrameHeight, uiFrameWidth * 4, bmdFormat8BitARGB, bmdFrameFlagDefault, &pDLVideoFrame) != S_OK)
			return false;

		uiTotalFrames = 0;

		resetFrame();
		setPreroll();

		pDLOutput->StartScheduledPlayback(0, frameTimescale, 1);

		return true;
	}

	void Output::resetFrame()
	{
		void* pFrame;
		pDLVideoFrame->GetBytes((void**)&pFrame);
		memset(pFrame, 0x00, pDLVideoFrame->GetRowBytes() * uiFrameHeight);
	}

	void Output::setPreroll()
	{
		for (uint32_t i = 0; i < 3; i++)
		{
			if (pDLOutput->ScheduleVideoFrame(pDLVideoFrame, (uiTotalFrames * frameDuration), frameDuration, frameTimescale) != S_OK)
				return;
			uiTotalFrames++;
		}
	}

	void Output::publishTexture(ofTexture &tex)
	{
		if (tex.getWidth() == uiFrameWidth
			&& tex.getHeight() == uiFrameHeight)
		{
			ofPixels pix2;
			tex.readToPixels(pix2);

			mutex.lock();
			if (!back_buffer->isAllocated() ||
				back_buffer->getWidth() != tex.getWidth() ||
				back_buffer->getHeight() != tex.getHeight()) {
				back_buffer->allocate(tex.getWidth(), tex.getHeight(), pix2.getNumChannels());
			}
			memcpy(&back_buffer->getData()[1], pix2.getData(), pix2.size() - 1);

			if (back_buffer->getNumChannels() != 4)
				back_buffer->setNumChannels(4);

			has_new_frame = true;

			mutex.unlock();
		}
		else
			ofLogError("ofxDeckLinkAPI::Output") << "invalid texture size";
	}

	void Output::publishPixels(ofPixels &pix)
	{
		if (pix.getWidth() == uiFrameWidth
			&& pix.getHeight() == uiFrameHeight)
		{
			mutex.lock();
			if (!back_buffer->isAllocated() ||
				back_buffer->getWidth() != pix.getWidth() ||
				back_buffer->getHeight() != pix.getHeight()) {
				back_buffer->allocate(pix.getWidth(), pix.getHeight(), pix.getNumChannels());
			}
			memcpy(&back_buffer->getData()[1], pix.getData(), pix.size() - 1);
			//*back_buffer = pix;

			if (back_buffer->getNumChannels() != 4)
				back_buffer->setNumChannels(4);

			has_new_frame = true;

			mutex.unlock();
		}
		else
			ofLogError("ofxDeckLinkAPI::Output") << "invalid pixel size";
	}

	void Output::setEnableQueueMode(bool enable, int numbuffer)
	{
		b_queue_mode = enable;
		ofPixels pix;
		pix.allocate(uiFrameWidth, uiFrameHeight, 4);
		pix.set(255);
		mutex.lock();
		while (!queued_pixels.empty()) {
			queued_pixels.pop();
		}
		for (int i = 0; i < numbuffer; ++i) {
			queued_pixels.push(pix);
		}
		mutex.unlock();
	}

	void Output::publishQueuedPixels(ofPixels & pix)
	{
		mutex.lock();
		queued_pixels.push(pix);
		auto* back_buffer = &queued_pixels.back();
		memcpy(&back_buffer->getData()[1], pix.getData(), pix.size() - 1);
		mutex.unlock();
	}

	//

	HRESULT Output::ScheduledFrameCompleted(IDeckLinkVideoFrame *completedFrame, BMDOutputFrameCompletionResult result)
	{
		if (pDLOutput == NULL)
		{
			return S_OK;
		}
		unique_lock<std::mutex> lock(lock_close);
		void* pFrame = NULL;
		pDLVideoFrame->GetBytes((void**)&pFrame);
		assert(pFrame != NULL);

		int num_schedule_frame = 1;

		if (result != bmdOutputFrameCompleted)
		{
			num_schedule_frame = 2;
		}

		mutex.lock();
		if (b_queue_mode) {
			if (!queued_pixels.empty()) {
				*front_buffer = queued_pixels.front();
				queued_pixels.pop();
			}
			else {
				ofLogError() << "queued pixels is empty";
			}
		}
		else {
			if (has_new_frame)
			{
				has_new_frame = false;
				std::swap(*front_buffer, *back_buffer);
			}
		}
		mutex.unlock();

		memcpy(pFrame, front_buffer->getPixels(), uiFrameWidth * uiFrameHeight * front_buffer->getNumChannels());

		for (int i = 0; i < num_schedule_frame; i++)
		{
			if (pDLOutput->ScheduleVideoFrame(pDLVideoFrame, (uiTotalFrames * frameDuration), frameDuration, frameTimescale) == S_OK)
				uiTotalFrames++;
		}

		cv.notify_all();

		return S_OK;
	}

}