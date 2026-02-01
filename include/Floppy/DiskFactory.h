// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DISKFACTORY_H
#define DISKFACTORY_H

#include <memory>
#include <string>
#include "Floppy/Disk.h"
#include "Floppy/D64.h"
#include "Floppy/D71.h"
#include "Floppy/D81.h"
#include "Floppy/G64.h"

enum class DiskFormat { D64, D71, D81, G64, Unknown};

class DiskFactory
{
    public:
        DiskFactory();
        virtual ~DiskFactory();

        static DiskFormat detectFormat(const std::string& path);

        // Create the right Disk subclass (but not yet load it)
        static std::unique_ptr<Disk> create(const std::string &path)
        {
            switch (detectFormat(path))
            {
              case DiskFormat::D64:
                  return std::make_unique<D64>();
              case DiskFormat::D71:
                  return std::make_unique<D71>();
              case DiskFormat::D81:
                  return std::make_unique<D81>();
              default:
                  return nullptr;
            }
        }

    protected:

    private:
};

#endif // DISKFACTORY_H
