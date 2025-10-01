// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef TAPEIMAGEFACTORY_H
#define TAPEIMAGEFACTORY_H

#include <algorithm>
#include <memory>
#include <string>
#include "TapeImage.h"
#include "TAP.h"
#include "T64.h"

std::unique_ptr<TapeImage> createTapeImage(const std::string& filePath);

#endif // TAPEIMAGEFACTORY_H
