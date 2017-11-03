TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

LIBS += -lptp2 -lusb

SOURCES += thetahdr.cpp
