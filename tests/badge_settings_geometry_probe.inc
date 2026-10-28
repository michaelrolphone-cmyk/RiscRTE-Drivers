/* Appended only to generated host test wrappers: no model mutation or I/O. */
__attribute__((visibility("default"))) void badge_settings_geometry_probe(int*v){
 int side=240,ox=(width()-240)/2,oy=(height()-240)/2;
#ifdef PORTABLE_SETTINGS_NATIVE_GEOMETRY
 side=sv_native_side();ox=sv_native_x(0);oy=sv_native_y(0);v[0]=1;
#else
 v[0]=0;
#endif
 native_point(&ox,&oy);v[1]=side;v[2]=ox;v[3]=oy;v[4]=(int)info.width;v[5]=(int)info.height;
 v[6]=sv_row_y(0)-sv_scroll[0]+SV_ROW_HEIGHT/2;v[7]=60-sv_scroll[1]+SV_ROW_HEIGHT/2;
 int controls[8][4]={{20,sv_row_y(0)-sv_scroll[0],200,SV_ROW_HEIGHT},{20,60-sv_scroll[1],200,SV_ROW_HEIGHT},
   {180,66,36,36},{82,192,76,26},{SV_SAVE_X,SV_ACTION_Y,SV_ACTION_W,SV_ACTION_H},
   {SV_CANCEL_X,SV_ACTION_Y,SV_ACTION_W,SV_ACTION_H},{20,88,64,44},{156,88,64,44}};
 for(unsigned i=0;i<8;i++){
  int x=controls[i][0],y=controls[i][1],w=controls[i][2],h=controls[i][3];
#ifdef PORTABLE_NOVA_UI
  if(i>=2)sv_geometry(&x,&y,&w,&h);
#endif
#ifdef PORTABLE_SETTINGS_NATIVE_GEOMETRY
  if(i>=2)sv_native_button_geometry(&x,&y,&w,&h);
  int x0=sv_native_x(x),y0=sv_native_y(y),x1=sv_native_x(x+w),y1=sv_native_y(y+h);
#else
  int x0=x+(width()-240)/2,y0=y+(height()-240)/2,x1=x0+w,y1=y0+h;
#endif
  native_point(&x0,&y0);native_point(&x1,&y1);
  v[8+i*4]=x0;v[9+i*4]=y0;v[10+i*4]=x1-x0;v[11+i*4]=y1-y0;
 }
 v[40]=sv_page==SV_ROOT;v[41]=sv_page==SV_FIELDS;v[42]=sv_page==SV_VALUE;
#ifdef PORTABLE_SETTINGS_SLEEP_TIMER
 v[43]=sv_page==SV_TIMER;
#else
 v[43]=0;
#endif
}
