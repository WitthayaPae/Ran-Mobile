# The client only uses the non-GUI helpers; the rest of Lib_Helper is MFC editor
# widgets (BmpButton, CoolDialogBar, ReportCtrl, ...) with no mobile equivalent.
set(Lib_Helper_SOURCES
  ${RAN_SRC}/Lib_Helper/HLibColorValue.cpp
  ${RAN_SRC}/Lib_Helper/HLibDataConvert.cpp
  ${RAN_SRC}/Lib_Helper/HLibTimeFunctions.cpp
  ${RAN_SRC}/Lib_Helper/StopWatch.cpp
)
