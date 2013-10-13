#pragma once
#include <string>
#include <wtypes.h>
#include <string>

#include "DeckLinkAPI_h.h"

#ifndef __func__
#define __func__ __FUNCTION__
#endif
#define OFXBM_WARNING ofLogWarning(string(__func__))
#define OFXBM_ERROR ofLogError(string(__func__))
#define OFXBM_FATAL ofLogFatalError(string(__func__))
#define CHECK_ERRORS(x, message) if ((x) != S_OK) { throw std::exception((std::string(__func__) + std::string(": ") +  std::string(message)).c_str()); }

namespace ofxBlackmagic {
	namespace Utils {
		//---------
		class CoManager {
		public:
			CoManager();
			~CoManager();
			IDeckLinkVideoConversion& getVideoConverter();
		protected:
			IDeckLinkVideoConversion* videoConverter;
		};

		//---------
		std::string BSTRToString(BSTR&);

		//---------
		extern CoManager CoManagerInstance;
	}
}