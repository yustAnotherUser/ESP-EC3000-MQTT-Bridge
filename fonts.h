// Font array (modify as needed)
// https://github.com/olikraus/u8g2/wiki/fntlist12#u8g2-fonts-capital-a-height-912
const uint8_t* fonts[] = {
  // 19 & 20 height
  // u8g2_font_spleen16x32_mf,         // 0 - NULL !!!
  // u8g2_font_inb19_mn,              // 1
  // u8g2_font_inr19_mn,           // 2
  // u8g2_font_luBIS19_tr,     // 3
  // u8g2_font_lubI19_tr,
  // u8g2_font_lubBI19_tr,
  // u8g2_font_lubB19_tr,
  
  //useable
  u8g2_font_crox4hb_tn,
  u8g2_font_logisoso18_tn,
  u8g2_font_logisoso16_tn,
  u8g2_font_crox5t_tr,
  u8g2_font_crox5tb_tf,
  u8g2_font_crox4tb_tn,
  u8g2_font_crox4t_tn,
  u8g2_font_crox3h_tn,
  u8g2_font_crox3hb_tn,
  u8g2_font_crox3tb_tn,
  u8g2_font_crox3t_tn, // 10
  u8g2_font_VCR_OSD_tr,
  u8g2_font_spleen12x24_mr,
  //14 height
  u8g2_font_balthasar_titling_nbp_tr, // eindeutig zu klein, lol
  u8g2_font_calibration_gothic_nbp_tr,
  u8g2_font_fub14_tr, // gut, dick, sehr leserlich, erscheint hell
  u8g2_font_chargen_92_mr, // wie oben, bisschen breiter
  u8g2_font_ncenR14_tr,     // gut, schmaler, sehr leserlich, erscheint weniger hell
  u8g2_font_helvR14_tr,// gut, dick, sehr leserlich, erscheint weniger hell
  u8g2_font_helvB14_tr,// gut, dick, sehr leserlich, erscheint heller
  u8g2_font_t0_17b_tn, // 20  minimalistisch, ein wenig kleiner aber sehr deutlich
  u8g2_font_9x18B_tn, // ok
  //13height
  u8g2_font_crox2tb_tn, // ok, eher klein
  u8g2_font_crox2hb_tn, // ok, eher klein
  //unsorted
  u8g2_font_lastapprenticebold_tr, // eigentlich ganz nett bisschen eng vllt.
  u8g2_font_cube_mel_tr,            // stylish - eher klein
  u8g2_font_press_mel_tr,           // stylish futuristisch - eher klein 
  u8g2_font_repress_mel_tr,         // stylish futuristisch - eher klein 
  u8g2_font_smart_patrol_nbp_tr,    // stylish futuristisch - eher klein 
  u8g2_font_missingplanet_tr,       // klein
  u8g2_font_ordinarybasis_tr, // 30
  u8g2_font_questgiver_tr,
  u8g2_font_seraphimb1_tr,
  u8g2_font_koleeko_tu,
  u8g2_font_tenthinguys_tr,
  u8g2_font_tenthinnerguys_tr,     
  u8g2_font_DigitalDisco_tr,
  u8g2_font_Terminal_tr
  // // 10 height
  // u8g2_font_mademoiselle_mel_tr,
  // u8g2_font_pieceofcake_mel_tr,
  // u8g2_font_tenfatguys_tr,
  // u8g2_font_sonicmania_tr,
  // u8g2_font_DigitalDiscoThin_tr,
  // u8g2_font_ncenB10_tr,
  // u8g2_font_pxplusibmvga9_tr,
  // // 11 height
  // u8g2_font_michaelmouse_tu,        // 30
  // u8g2_font_squirrel_tr,
  // u8g2_font_fewture_tr,
  // u8g2_font_adventurer_tr, //nice
  // u8g2_font_Pixellari_tr,   //nice
  // // 12 height
  // u8g2_font_cupcakemetoyourleader_tr, //sehr nice - letzte pixelreihe aktuell abgeschnitten
  // u8g2_font_heavybottom_tr,
  // u8g2_font_12x6LED_tr,             // 37
};
const uint8_t numFonts = sizeof(fonts) / sizeof(fonts[0]);
