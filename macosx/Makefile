################################################################################
#
# Simple makefile for building on Mac OS X with the
# Project Builder command line tool 'pbxbuild'
#
# RCS: @(#) $Id: Makefile,v 1.5 2002/10/09 13:02:08 das Exp $
#
################################################################################

INSTALL_ROOT	?=

BUILD_DIR	?= ${CURDIR}/../../build

TARGET		= Wish

DEVBUILDSTYLE	= Development
DEPBUILDSTYLE	= Deployment

PBXBUILD	= /usr/bin/pbxbuild

BUILD		= ${PBXBUILD} SYMROOT="${BUILD_DIR}" -target "${TARGET}"

DEVBUILD	= ${BUILD} -buildstyle "${DEVBUILDSTYLE}"
DEPBUILD	= ${BUILD} -buildstyle "${DEPBUILDSTYLE}"

INSTALLOPTS	= INSTALL_ROOT="${INSTALL_ROOT}"

EMBEDDEDOPTS	= DYLIB_INSTALL_PATH="@executable_path/../Frameworks"

################################################################################

all: develop deploy

install: install-develop install-deploy

embedded: embedded-deploy

install-embedded: install-embedded-deploy cleanup-embedded forceRelink forceRelinkTcl

clean: clean-develop clean-deploy

################################################################################

develop:
	${DEVBUILD}

deploy:
	${DEPBUILD}

install-develop: 
	${DEVBUILD} install ${INSTALLOPTS}

install-deploy:
	${DEPBUILD} install ${INSTALLOPTS}

embedded-develop: forceRelink
	${DEVBUILD} ${EMBEDDEDOPTS}

embedded-deploy: forceRelink
	${DEPBUILD} ${EMBEDDEDOPTS}

install-embedded-develop:
	${DEVBUILD} install ${INSTALLOPTS} ${EMBEDDEDOPTS}

install-embedded-deploy:
	${DEPBUILD} install ${INSTALLOPTS} ${EMBEDDEDOPTS}

clean-develop:
	${DEVBUILD} clean

clean-deploy:
	${DEPBUILD} clean

################################################################################

forceRelink:
	@-cd ${BUILD_DIR}; \
	rm -rf Tk.framework Wish\ Shell.app libtkstub8.4.a

forceRelinkTcl:
	@-cd ${BUILD_DIR}; \
	rm -rf Tcl.framework \
	Development.build/Tcl.build/Tcl Deployment.build/Tcl.build/Tcl

cleanup-embedded:
	@-cd ${INSTALL_ROOT}; \
	rm -f Frameworks; \
	rm -rf @executable_path; \
	rm -rf Library/Frameworks/Tcl.framework; \
	rm -rf Library/Frameworks/Tk.framework; \
	rmdir -p Library/Frameworks 2>&-;

################################################################################

.PHONY: all install embedded clean develop deploy install-develop install-deploy \
embedded-develop embedded-deploy install-embedded-develop install-embedded-deploy \
clean-develop clean-deploy forceRelinkTcl \
cleanup-embedded

################################################################################
