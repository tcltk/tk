################################################################################
#
# Simple makefile for building on Mac OS X with the
# Project Builder command line tool 'pbxbuild'
#
# RCS: @(#) $Id: Makefile,v 1.1.2.1 2002/08/30 18:18:16 das Exp $
#
################################################################################

INSTALL_ROOT	=

TARGET		= Wish

DEVBUILDSTYLE	= Development
DEPBUILDSTYLE	= Deployment

PBXBUILD	= /usr/bin/pbxbuild

DEVBUILD	= ${PBXBUILD} -target "${TARGET}" -buildstyle "${DEVBUILDSTYLE}"
DEPBUILD	= ${PBXBUILD} -target "${TARGET}" -buildstyle "${DEPBUILDSTYLE}"

################################################################################

all: develop deploy

install: install-develop install-deploy

clean: clean-develop clean-deploy

################################################################################

develop:
	${DEVBUILD}

deploy:
	${DEPBUILD}

install-develop: 
	${DEVBUILD} install INSTALL_ROOT="${INSTALL_ROOT}"

install-deploy:
	${DEPBUILD} install INSTALL_ROOT="${INSTALL_ROOT}"

clean-develop:
	${DEVBUILD} clean

clean-deploy:
	${DEPBUILD} clean

################################################################################
