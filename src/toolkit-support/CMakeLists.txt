#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

if (NOT TARGET toolkit_support)

    pico_add_library(toolkit_support)

	target_include_directories(toolkit_support_headers INTERFACE include)
	
    target_sources(toolkit_support INTERFACE
    )
    
    target_link_libraries(toolkit_support INTERFACE
		pico_atomic
		cmsis_core
	)
    

endif()
