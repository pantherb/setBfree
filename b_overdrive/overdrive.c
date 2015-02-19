#ifndef CONFIGDOCONLY
/* Interpolation filter at digital frequency 0.25 */
/* Decimation filter at digital frequency 0.25 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "overdrive.h"


/* Decimation filter definition */
static const float aaldef[33] = {
  -0.000000000000000001561469772894539464010892418355247457384394,
  -0.001887878403067588806152343750000000000000000000000000000000,
   0.000000000000000002244913588548488609774461677304202567029279,
   0.003862478304654359817504882812500000000000000000000000000000,
  -0.000000000000000004191197191582164338825335081484269039719948,
  -0.008242466486990451812744140625000000000000000000000000000000,
   0.000000000000000007104016524965676315133203400087325007916661,
   0.015947114676237106323242187500000000000000000000000000000000,
  -0.000000000000000010539920475899652678713341868999009420804214,
  -0.028676560148596763610839843750000000000000000000000000000000,
   0.000000000000000013975825254014241595321155209319385903654620,
   0.050718560814857482910156250000000000000000000000000000000000,
  -0.000000000000000016888644173807447295115186092218095836869907,
  -0.098015904426574707031250000000000000000000000000000000000000,
   0.000000000000000018834927363250816747652222060693816274579149,
   0.315941751003265380859375000000000000000000000000000000000000,
   0.500705778598785400390625000000000000000000000000000000000000,
   0.315941751003265380859375000000000000000000000000000000000000,
   0.000000000000000018834927363250816747652222060693816274579149,
  -0.098015904426574707031250000000000000000000000000000000000000,
  -0.000000000000000016888644173807447295115186092218095836869907,
   0.050718560814857482910156250000000000000000000000000000000000,
   0.000000000000000013975825254014241595321155209319385903654620,
  -0.028676560148596763610839843750000000000000000000000000000000,
  -0.000000000000000010539920475899652678713341868999009420804214,
   0.015947114676237106323242187500000000000000000000000000000000,
   0.000000000000000007104016524965676315133203400087325007916661,
  -0.008242466486990451812744140625000000000000000000000000000000,
  -0.000000000000000004191197191582164338825335081484269039719948,
   0.003862478304654359817504882812500000000000000000000000000000,
   0.000000000000000002244913588548488609774461677304202567029279,
  -0.001887878403067588806152343750000000000000000000000000000000,
  -0.000000000000000001561469772894539464010892418355247457384394
};


/* Weight count for wi[][] above. */
static const int wiLen[4] = {
  9,
  8,
  8,
  8
};

/* Interpolation filter definition */
static const float ipwdef[33] = {
  -0.000000000000000001561469772894539464010892418355247457384394,
  -0.001887878403067588806152343750000000000000000000000000000000,
   0.000000000000000002244913588548488609774461677304202567029279,
   0.003862478304654359817504882812500000000000000000000000000000,
  -0.000000000000000004191197191582164338825335081484269039719948,
  -0.008242466486990451812744140625000000000000000000000000000000,
   0.000000000000000007104016524965676315133203400087325007916661,
   0.015947114676237106323242187500000000000000000000000000000000,
  -0.000000000000000010539920475899652678713341868999009420804214,
  -0.028676560148596763610839843750000000000000000000000000000000,
   0.000000000000000013975825254014241595321155209319385903654620,
   0.050718560814857482910156250000000000000000000000000000000000,
  -0.000000000000000016888644173807447295115186092218095836869907,
  -0.098015904426574707031250000000000000000000000000000000000000,
   0.000000000000000018834927363250816747652222060693816274579149,
   0.315941751003265380859375000000000000000000000000000000000000,
   0.500705778598785400390625000000000000000000000000000000000000,
   0.315941751003265380859375000000000000000000000000000000000000,
   0.000000000000000018834927363250816747652222060693816274579149,
  -0.098015904426574707031250000000000000000000000000000000000000,
  -0.000000000000000016888644173807447295115186092218095836869907,
   0.050718560814857482910156250000000000000000000000000000000000,
   0.000000000000000013975825254014241595321155209319385903654620,
  -0.028676560148596763610839843750000000000000000000000000000000,
  -0.000000000000000010539920475899652678713341868999009420804214,
   0.015947114676237106323242187500000000000000000000000000000000,
   0.000000000000000007104016524965676315133203400087325007916661,
  -0.008242466486990451812744140625000000000000000000000000000000,
  -0.000000000000000004191197191582164338825335081484269039719948,
   0.003862478304654359817504882812500000000000000000000000000000,
   0.000000000000000002244913588548488609774461677304202567029279,
  -0.001887878403067588806152343750000000000000000000000000000000,
  -0.000000000000000001561469772894539464010892418355247457384394
};



struct b_preamp {
  /* Input history buffer */
  float xzb[64];
  /* Input history writer */
  float * xzp;
  /* Input history end sentinel */
  float * xzpe;
  
  /* Negative index access wrap sentinel */
  float * xzwp;
  /* Transfer-function output history buffer */
  float yzb[128];
  /* Transfer-function output writer */
  float * yzp;
  /* Transfer-function output history end sentinel */
  float * yzpe;
  /* Transfer-function output history wrap sentinel */
  float * yzwp;
  /* Zero-filled filter of interpolation length */
  float ipolZeros[33];
  /* Sample-specific runtime interpolation FIRs */
  float wi[4][9];
  /* Decimation filter runtime */
  float aal[33];
  /* Decimation filter end sentinel */
  float * aalEnd;
  size_t ipolFilterLength;
  size_t aalFilterLength;
  /* Zero-filled filter of anti-aliasing length */
  float aalZeros[33];
  /* Clean/overdrive switch */
  int isClean;
  float outputGain;
  
  /* Input gain */
  float inputGain;
  float sagZ;
  float sagFb;
  /* Variables for the inverted and biased transfer function */
  float biasBase;
  /* bias and norm are set in function cfg_biased() */
  float bias;
  float norm;
  /* ovt_biased : One sample memory */
  float adwZ;
  /* ovt_biased : Positive feedback */
  float adwFb;
  
  float adwZ1;
  float adwFb2;
  float adwGfb;
  float adwGfZ;
  float sagZgb;
};
/*  *** END STRUCT *** */





/* Remember to call this first with the filter definitions */
/* ipolDef is the interpolation filter definition */
/* aalDef is the anti-aliasing filter definition */
static void mixFilterWeights (void *pa, const float * ipolDef, const float * aalDef) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  int i;
  float sum = 0.0;
  
  float mix[33];
  
  /* Copy the interpolation filter weights */
  
  for (i = 0; i < 33; i++) {
    mix[i] = ipolDef[i];
    sum += fabs (mix[i]);
  }
  
  /* Normalize the copy */
  
  for (i = 0; i < 33; i++) {
    mix[i] /= sum;
  }
  
  /* Install in correct sequence in runtime array of weights */
  
  pp->wi[0][0] = mix[0];
  pp->wi[0][1] = mix[4];
  pp->wi[0][2] = mix[8];
  pp->wi[0][3] = mix[12];
  pp->wi[0][4] = mix[16];
  pp->wi[0][5] = mix[20];
  pp->wi[0][6] = mix[24];
  pp->wi[0][7] = mix[28];
  pp->wi[0][8] = mix[32];
  pp->wi[1][0] = mix[3];
  pp->wi[1][1] = mix[7];
  pp->wi[1][2] = mix[11];
  pp->wi[1][3] = mix[15];
  pp->wi[1][4] = mix[19];
  pp->wi[1][5] = mix[23];
  pp->wi[1][6] = mix[27];
  pp->wi[1][7] = mix[31];
  pp->wi[2][0] = mix[2];
  pp->wi[2][1] = mix[6];
  pp->wi[2][2] = mix[10];
  pp->wi[2][3] = mix[14];
  pp->wi[2][4] = mix[18];
  pp->wi[2][5] = mix[22];
  pp->wi[2][6] = mix[26];
  pp->wi[2][7] = mix[30];
  pp->wi[3][0] = mix[1];
  pp->wi[3][1] = mix[5];
  pp->wi[3][2] = mix[9];
  pp->wi[3][3] = mix[13];
  pp->wi[3][4] = mix[17];
  pp->wi[3][5] = mix[21];
  pp->wi[3][6] = mix[25];
  pp->wi[3][7] = mix[29];
  
  /* Copy the anti-aliasing filter definition */
  
  sum = 0.0;
  for (i = 0; i < 33; i++) {
    mix[i] = aalDef[i];
    sum += fabs (mix[i]);
  }
  
  /* Normalize the weights to unit gain and install */
  
  for (i = 0; i < 33; i++) {
    pp->aal[i] = mix[i] / sum;
  }
} /* preFilterCompile */



float * overdrive (void *pa, const float * inBuf, float * outBuf, size_t buflen)
{
  struct b_preamp *pp = (struct b_preamp *) pa;
  const float * xp = inBuf;
  float * yp = outBuf;
  int i;
  size_t n;
  
  for (n = 0; n < buflen; n++) {
    float xin;
    float u = 0.0;
    float v;
    float y = 0.0;
    
    /* Place the next input sample in the input history. */
    if (++(pp->xzp) == pp->xzpe) {
      pp->xzp = pp->xzb;
    }
    
    xin = pp->inputGain * (*xp++);
    pp->sagZ = (pp->sagFb * pp->sagZ) + fabsf(xin);
    pp->bias = pp->biasBase - (pp->sagZgb * pp->sagZ);
    pp->norm = 1.0 - (1.0 / (1.0 + (pp->bias * pp->bias)));
    *(pp->xzp) = xin;
    
    /* Check the input history wrap sentinel */
    if (pp->xzwp <= pp->xzp) {
      for (i = 0; i < 4; i++) {
        
        /* wp is ptr to interpol. filter weights for this sample */
        float * wp = &(pp->wi[i][0]);
        
        /* wpe is FIR weight end sentinel */
        float * wpe = wp + wiLen[i];
        
        /* xr is ptr to samples in input history */
        float * xr = pp->xzp;
        
        /* Apply convolution */
        while (wp < wpe) {
          u += ((*wp++) * (*xr--));
        }
      }
    }
    else {
      /* Wrapping code */
      for (i = 0; i < 4; i++) {
        
        /* Interpolation weights for this sample */
        float * wp = &(pp->wi[i][0]);
        /* Weight end sentinel */
        float * wpe = wp + wiLen[i];
        /* Input history read pointer */
        float * xr = pp->xzp;
        
        while (pp->xzb <= xr) {
          u += ((*wp++) * (*xr--));
        }
        
        xr = &(pp->xzb[63]);
        
        while (wp < wpe) {
          u += ((*wp++) * (*xr--));
        }
      }
    }
    
    /* Apply transfer function */
    /* v = T (u); */
    /* Adaptive linear-non-linear transfer function */
    /* Global negative feedback */
    u -= (pp->adwGfb * pp->adwGfZ);
    {
      float temp = u - pp->adwZ;
      pp->adwZ = u + (pp->adwZ * pp->adwFb);
      u = temp;
    }
    if (u < 0.0) {
      float x2 = u - pp->bias;
      v = (1.0 / (1.0 + (x2 * x2))) - 1.0 + pp->norm;
    } else {
      float x2 = u + pp->bias;
      v = 1.0 - pp->norm - (1.0 / (1.0 + (x2 * x2)));
    }
    {
      float temp = v + (pp->adwFb2 * pp->adwZ1);
      v = temp - pp->adwZ1;
      pp->adwZ1 = temp;
    }
    /* Global negative feedback */
    pp->adwGfZ = v;
    
    /* Put transferred sample in output history. */
    if (++pp->yzp == pp->yzpe) {
      pp->yzp = pp->yzb;
    }
    *(pp->yzp) = v;
    
    /* Decimation */
    if (pp->yzwp <= pp->yzp) {
      /* No-wrap code */
      /* wp points to weights in the decimation FIR */
      float * wp = pp->aal;
      float * yr = pp->yzp;
      
      /* Convolve with decimation filter. */
      while (wp < pp->aalEnd) {
        y += ((*wp++) * (*yr--));
      }
    }
    else {
      /* Wrap code */
      float * wp = pp->aal;
      float * yr = pp->yzp;
      
      while (pp->yzb <= yr) {
        y += ((*wp++) * (*yr--));
      }
      
      yr = &(pp->yzb[127]);
      
      while (wp < pp->aalEnd) {
        y += ((*wp++) * (*yr--));
      }
    }
    
    *yp++ = pp->outputGain * y;
  }
  /* End of for-loop over input buffer */
  return outBuf;
} /* overdrive */



/* Adapter function */
float * preamp (void * pa,
                float * inBuf,
                float * outBuf,
                size_t bufLengthSamples) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  if (pp->isClean) {
    memcpy(outBuf, inBuf, bufLengthSamples*sizeof(float));
  }
  else {
    overdrive (pa, inBuf, outBuf, bufLengthSamples);
  }
  
  return outBuf;
}


void * allocPreamp () {
  struct b_preamp *pp = (struct b_preamp *) calloc(1, sizeof(struct b_preamp));
  pp->xzp = &(pp->xzb[0]);
  pp->xzpe = &(pp->xzb[64]);
  pp->xzwp = &(pp->xzb[9]);
  pp->yzp = &(pp->yzb[0]);
  pp->yzpe = &(pp->yzb[128]);
  pp->yzwp = &(pp->yzb[33]);
  pp->aalEnd = &(pp->aal[33]);
  pp->ipolFilterLength = 33;
  pp->aalFilterLength = 33;
  pp->isClean = 1;
  pp->outputGain = 0.8795;
  pp->inputGain = 3.5675;
  
  
  
  pp->sagZ = 0.0;
  pp->sagFb = 0.991;
  pp->sagFb = 0.991;
  pp->biasBase = 0.5347;
  pp->adwZ = 0.0;
  pp->adwFb = 0.5821;
  pp->adwZ1 = 0.0;
  pp->adwFb2 = 0.999;
  pp->adwGfb = -0.6214;
  pp->adwGfZ = 0.0;
  pp->sagZgb = 0.0094;
  return pp;
}


void freePreamp (void * pa) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  free(pp);
}



void setClean (void *pa, int useClean) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  pp->isClean = useClean ? 1: 0;
}
void setCleanCC (void *pa, unsigned char uc) {
  setClean(pa, uc > 63 ? 0 : 1);
}



/* Legacy function */
int ampConfig (void *pa, ConfigContext * cfg) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  int rtn = 1;
  float v = 0;
  
  /* Config generated by overmaker */
  
  if (getConfigParameter_f ("overdrive.inputgain", cfg, &pp->inputGain)) return 1;
  else if (getConfigParameter_f ("overdrive.outputgain", cfg, &pp->outputGain)) return 1;
  else if (getConfigParameter_f ("xov.ctl_biased_gfb", cfg, &v)) { fctl_biased_gfb(pp, v); return 1; }
  else if (getConfigParameter_f ("xov.ctl_biased", cfg, &v)) { fctl_biased(pp, v); return 1; }
  else if (getConfigParameter_f ("overdrive.character", cfg, &v)) { fctl_biased_fat(pp, v); return 1; }
  
  /* Config generated by external module */
  
  
  if (getConfigParameter_fr ("xov.ctl_biased_fb", cfg, &pp->adwFb, 0, 0.999));
  else if (getConfigParameter_fr ("xov.ctl_biased_fb2", cfg, &pp->adwFb2, 0, 0.999));
  else if (getConfigParameter_f ("xov.ctl_sagtobias", cfg, &pp->sagFb));
  else return 0;
  return rtn;
}




/* Computes the constants for transfer curve */
void cfg_biased (void *pa, float new_bias) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  if (0.0 < new_bias) {
    pp->biasBase = new_bias;
    /* If power sag emulation is enabled bias is set there. */
    pp->bias = pp->biasBase;
    pp->norm = 1.0 - (1.0 / (1.0 + (pp->bias * pp->bias)));
  }
}
void fctl_biased (void *pa, float u) {
  float v = 0 + ((0.7 - 0) * (u * u));
  cfg_biased (pa, v);
}

void ctl_biased (void *d, unsigned char uc) {
  fctl_biased (d, uc/127.0);
}



/* ovt_biased:Sets the positive feedback */
void fctl_biased_fb (void *pa, float u) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  pp->adwFb = 0.999 * u;
  printf ("\rFbk=%10.4f", pp->adwFb);
  fflush (stdout);
}

void ctl_biased_fb (void *d, unsigned char uc) {
  fctl_biased_fb (d, uc/127.0);
}



/* ovt_biased: Sets sag impact */
void fctl_sagtoBias (void *pa, float u) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  pp->sagZgb = 0 + ((0.05 - 0) * u);
  printf ("\rpp->ZGB=%10.4f", pp->sagZgb);
  fflush (stdout);
}

void ctl_sagtoBias (void *d, unsigned char uc) {
  fctl_sagtoBias (d, uc/127.0);
}



/* ovt_biased: Postdiff feedback control */
void fctl_biased_fb2 (void *pa, float u) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  pp->adwFb2 = 0.999 * u;
  printf ("\rFb2=%10.4f", pp->adwFb2);
  fflush (stdout);
}

void ctl_biased_fb2 (void *d, unsigned char uc) {
  fctl_biased_fb2 (d, uc/127.0);
}



/* ovt_biased: Global feedback control */
void fctl_biased_gfb (void *pa, float u) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  pp->adwGfb = -0.999 * u;
  printf ("\rGfb=%10.4f", pp->adwGfb);
  fflush (stdout);
}

void ctl_biased_gfb (void *d, unsigned char uc) {
  fctl_biased_gfb (d, uc/127.0);
}



/* ovt_biased: Fat control */
void ctl_biased_fat (void *pa, unsigned char uc) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  if (uc < 64) {
    if (uc < 32) {
      pp->adwFb = 0.5821;
      pp->adwFb2 = 0.999 + ((0.5821 - 0.999) * (((float) uc) / 31.0));
    } else {
      pp->adwFb = 0.5821 + ((0.999 - 0.5821) * (((float) (uc - 32)) / 31.0));
      pp->adwFb2 = 0.5821;
    }
  } else {
    pp->adwFb = 0.999;
    pp->adwFb2 = 0.5821 + ((0.999 - 0.5821) * (((float) (uc - 64)) / 63.0));
  }
}

void fctl_biased_fat (void *d, float f) {
  ctl_biased_fat (d, (unsigned char)(f*127.0));
}



void setInputGain (void *pa, unsigned char uc) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  pp->inputGain = 0.001 + ((10 - 0.001) * (((float) uc) / 127.0));
  printf ("\rINP:%10.4lf", pp->inputGain);
  fflush (stdout);
}

void fsetInputGain (void *d, float f) {
  setInputGain (d, (unsigned char)(f*127.0));
}



void setOutputGain (void *pa, unsigned char uc) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  pp->outputGain = 0.1 + ((10 - 0.1) * (((float) uc) / 127.0));
  printf ("\rOUT:%10.4lf", pp->outputGain);
  fflush (stdout);
}

void fsetOutputGain (void *d, float f) {
  setOutputGain (d, (unsigned char)(f*127.0));
}



/* Legacy function */
void initPreamp (void *pa, void *m) {
  struct b_preamp *pp = (struct b_preamp *) pa;
  mixFilterWeights (pa, ipwdef, aaldef);
  useMIDIControlFunction (m, "xov.ctl_biased", ctl_biased, pa);
  useMIDIControlFunction (m, "xov.ctl_biased_fb", ctl_biased_fb, pa);
  useMIDIControlFunction (m, "xov.ctl_biased_fb2", ctl_biased_fb2, pa);
  useMIDIControlFunction (m, "xov.ctl_biased_gfb", ctl_biased_gfb, pa);
  useMIDIControlFunction (m, "xov.ctl_sagtobias", ctl_sagtoBias, pa);
  useMIDIControlFunction (m, "overdrive.character", ctl_biased_fat, pa);
  cfg_biased (pa, 0.5347);
  pp->adwFb = 0.5821;
  useMIDIControlFunction (m, "overdrive.enable", setCleanCC, pa);
  useMIDIControlFunction (m, "overdrive.inputgain", setInputGain, pa);
  useMIDIControlFunction (m, "overdrive.outputgain", setOutputGain, pa);
}
#else // no CONFIGDOCONLY
# include "cfgParser.h"
#endif


static const ConfigDoc doc[] = {
  {"overdrive.inputgain", CFG_FLOAT, "0.3567", "This is how much the input signal is scaled as it enters the overdrive effect. The default value is quite hot, but you can of course try it in anyway you like; range [0..1]", INCOMPLETE_DOC},
  {"overdrive.outputgain", CFG_FLOAT, "0.07873", "This is how much the signal is scaled as it leaves the overdrive effect. Essentially this value should be as high as possible without clipping (and you *will* notice when it does - Test with a bass-chord on 88 8888 000 with percussion enabled and full swell, but do turn down the amplifier/headphone volume first!); range [0..1]", INCOMPLETE_DOC},
  {"xov.ctl_biased", CFG_FLOAT, "0.5347", "bias base; range [0..1]", INCOMPLETE_DOC},
  {"xov.ctl_biased_gfb", CFG_FLOAT, "0.6214", "Global [negative] feedback control; range [0..1]", INCOMPLETE_DOC},
  {"overdrive.character", CFG_FLOAT, "-", "Abstraction to set xov.ctl_biased_fb and xov.ctl_biased_fb2", INCOMPLETE_DOC},
  {"xov.ctl_biased_fb", CFG_FLOAT, "0.5821", "This parameter behaves somewhat like an analogue tone control for bass mounted before the overdrive stage. Unity is somewhere around the value 0.6, lesser values takes away bass and lowers the volume while higher values gives more bass and more signal into the overdrive. Must be less than 1.0.", INCOMPLETE_DOC},
  {"xov.ctl_biased_fb2", CFG_FLOAT, "0.999", "The fb2 parameter has the same function as fb1 but controls the signal after the overdrive stage. Together the two parameters are useful in that they can reduce the amount of bass going into the overdrive and then recover it on the other side. Must be less than 1.0.", INCOMPLETE_DOC},
  {"xov.ctl_sagtobias", CFG_FLOAT, "0.1880", "This parameter is part of an attempt to recreate an artefact called 'power sag'. When a power amplifier is under heavy load the voltage drops and alters the operating parameters of the unit, usually towards more and other kinds of distortion. The sagfb parameter controls the rate of recovery from the sag effect when the load is lifted. Must be less than 1.0.", INCOMPLETE_DOC},
  DOC_SENTINEL
};


const ConfigDoc *ampDoc () {
  return doc;
}
