#ifndef RAN_UI_PAN_H
#define RAN_UI_PAN_H

//  See ui_pan.cpp. Recomputed once a frame; read by the 2D draw path and by the
//  touch path, which must agree or a press lands somewhere other than where it
//  looks.
extern "C" void  RanUIPan_Update ( void );
extern "C" float RanUIPan_Y ( void );

#endif
