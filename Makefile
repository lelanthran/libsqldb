
LIBNAME=my_library_name

#############################################################
# You should not need to modify anything below this comment #
#############################################################

MAKEPROGRAM_EXE=$(findstring exe,$(MAKE))
MAKEPROGRAM_MINGW=$(findstring mingw,$(MAKE))

# Remember that freebsd uses gmake/gnu-make, not plain make

ifneq ($(MAKEPROGRAM_EXE),)
	# We are running on Windows for certain - not sure if cygwin or not
	PLATFORM=$(shell windows/platform_name.bat)
	INCFILE=windows/Makefile.mingw
endif

ifneq ($(MAKEPROGRAM_EXE),)
	# We are running on Windows/Mingw
	PLATFORM=$(shell windows/platform_name.bat)
endif

# If neither of the above are true then we assume a posix platform
ifeq ($(PLATFORM),)
	PLATFORM=$(shell posix/platform_name.sh)
	INCFILE=posix/Makefile.posix
endif

include $(INCFILE)

