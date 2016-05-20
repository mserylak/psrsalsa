/*
Copyright (c) 2015, Patrick Weltevrede
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gsl/gsl_sort_float.h>
#include <gsl/gsl_statistics_float.h>
#include <gsl/gsl_integration.h>
#include "psrsalsa.h"




int filterPApoints(datafile_definition *datafile, verbose_definition verbose)
{
  int dPa_polnr;
  long i, j, nrpoints;
  float *olddata;
  if(datafile->poltype != POLTYPE_ILVPAdPA && datafile->poltype != POLTYPE_PAdPA) {
    printerror(verbose.debug, "ERROR filterPApoints: Data doesn't appear to have poltype ILVPAdPA or PAdPA.");
    return 0;
  }
  if(datafile->poltype == POLTYPE_ILVPAdPA && datafile->NrPols != 5) {
    printerror(verbose.debug, "ERROR filterPApoints: 5 polarization channels were expected, but there are only %ld.", datafile->NrPols);
    return 0;
  }else if(datafile->poltype == POLTYPE_PAdPA && datafile->NrPols != 2) {
    printerror(verbose.debug, "ERROR filterPApoints: 2 polarization channels were expected, but there are only %ld.", datafile->NrPols);
    return 0;
  }
  if(datafile->NrSubints > 1 || datafile->NrFreqChan > 1) {
    printerror(verbose.debug, "ERROR filterPApoints: Can only do this opperation if there is one subint and one frequency channel.");
    return 0;
  }
  if(datafile->tsampMode != TSAMPMODE_LONGITUDELIST) {
    printerror(verbose.debug, "ERROR filterPApoints: Expected pulse longitudes to be defined.");
    return 0;
  }

  if(datafile->poltype == POLTYPE_ILVPAdPA) {
    dPa_polnr = 4;
  }else if(datafile->poltype == POLTYPE_PAdPA) {
    dPa_polnr = 1;
  }



  nrpoints = 0;
  for(i = 0; i < datafile->NrBins; i++) {
    if(datafile->data[i+dPa_polnr*datafile->NrBins] > 0) {
      nrpoints++;
    }
  }
  if(verbose.verbose)
    printf("Keeping %ld significant PA points\n", nrpoints);

  olddata = datafile->data;
  datafile->data = (float *)malloc(nrpoints*datafile->NrPols*sizeof(float));
  if(datafile->data == NULL) {
    printerror(verbose.debug, "ERROR filterPApoints: Memory allocation error.");
    return 0;
  }



  j = 0;
  for(i = 0; i < datafile->NrBins; i++) {
    if(olddata[i+dPa_polnr*datafile->NrBins] > 0) {
      if(datafile->poltype == POLTYPE_ILVPAdPA) {
 datafile->data[j+0*nrpoints] = olddata[i+0*datafile->NrBins];
 datafile->data[j+1*nrpoints] = olddata[i+1*datafile->NrBins];
 datafile->data[j+2*nrpoints] = olddata[i+2*datafile->NrBins];
 datafile->data[j+3*nrpoints] = olddata[i+3*datafile->NrBins];
 datafile->data[j+4*nrpoints] = olddata[i+4*datafile->NrBins];
      }else {
 datafile->data[j+0*nrpoints] = olddata[i+0*datafile->NrBins];
 datafile->data[j+1*nrpoints] = olddata[i+1*datafile->NrBins];
      }
      datafile->tsamp_list[j] = datafile->tsamp_list[i];
      j++;
    }
  }
  free(olddata);
  datafile->NrBins = nrpoints;
  return datafile->NrBins;
}
int make_paswing_fromIQUV(datafile_definition *datafile, float *Ppulse, regions_definition onpulse, int normalize, int correctLbias, float correctQV, float correctV, float loffset, verbose_definition verbose)
{
  int indent;
  long i, j, NrOffpulseBins, pulsenr, freqnr;
  float ymax, I, RMSQ, RMSU, RMSP, *Loffpulse, *Poffpulse, medianL, medianP, *newdata;
  if(verbose.verbose) {
    for(indent = 0; indent < verbose.indent; indent++) printf(" ");
    printf("Constructing PA and degree of linear polarization\n");
    for(indent = 0; indent < verbose.indent; indent++) printf(" ");
    printf("  Reference frequency for PA is ");
    if(datafile->isDeFarad) {
      if(datafile->freq_ref > -1.1 && datafile->freq_ref < -0.9)
 printf("infinity\n");
      else if(datafile->freq_ref < 0)
 printf("unknown\n");
      else
 printf("%f MHz\n", datafile->freq_ref);
    }else {
      if(datafile->NrFreqChan == 1)
 printf("%f MHz\n", get_centre_freq(*datafile, verbose));
      else
 printf("observing frequencies of individual frequency channels\n");
    }
    for(indent = 0; indent < verbose.indent; indent++) printf(" ");
    printf("  ");
    switch(correctLbias) {
    case -1: printf("No L de-bias applied"); break;
    case 0: printf("De-bias L using median noise subtraction"); break;
    case 1: printf("De-bias L using Wardle & Kronberg correction"); break;
    default: printerror(verbose.debug, "ERROR make_paswing_fromIQUV: Undefined L de-bias method specified."); return 0;
    }
    if(correctQV != 1 || correctV != 1)
      printf(", Q correction factor %f, V correction factor %f", 1.0/correctQV, 1.0/(correctQV*correctV));
    if(normalize)
      printf(", output is normalised");
    if(loffset != 0)
      printf(", pulse longitude shifted by %f deg\n", loffset);
    printf("\n");
  }
  if(datafile->NrPols != 4) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR make_paswing_fromIQUV: Expected 4 input polarizations.");
    return 0;
  }
  if(datafile->poltype != POLTYPE_STOKES) {
    if(datafile->poltype == POLTYPE_UNKNOWN) {
      printwarning(verbose.debug, "WARNING make_paswing_fromIQUV: Polarization state unknown, it is assumed the data are Stokes parameters.");
    }else {
      printerror(verbose.debug, "ERROR make_paswing_fromIQUV: Convert data into Stokes parameters first.");
      return 0;
    }
  }
  if(datafile->tsampMode != TSAMPMODE_FIXEDTSAMP) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR make_paswing_fromIQUV: Expects input to have a regular sampling.");
    return 0;
  }
  if(correctQV == 0) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR make_paswing_fromIQUV: correctQV is set to zero, you probably want this to be 1.");
    return 0;
  }
  if(correctV == 0) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR make_paswing_fromIQUV: correctV is set to zero, you probably want this to be 1.");
    return 0;
  }
  if(datafile->isDebase == 0) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR make_paswing_fromIQUV: Please remove baseline first, i.e. use pmod -debase.");
    return 0;
  }else if(datafile->isDebase != 1) {
    fflush(stdout);
    printwarning(verbose.debug, "WARNING make_paswing_fromIQUV: Unknown baseline state. It is assumed the baseline has already removed from the data.");
  }
  if(datafile->offpulse_rms != NULL) {
    free(datafile->offpulse_rms);
  }
  Loffpulse = (float *)malloc((datafile->NrBins)*sizeof(float));
  Poffpulse = (float *)malloc((datafile->NrBins)*sizeof(float));
  newdata = (float *)malloc(datafile->NrBins*datafile->NrSubints*datafile->NrFreqChan*5*sizeof(float));
  datafile->offpulse_rms = (float *)malloc(datafile->NrSubints*datafile->NrFreqChan*5*sizeof(float));
  datafile->tsamp_list = (double *)malloc(datafile->NrBins*sizeof(double));
  if(Loffpulse == NULL || Poffpulse == NULL || newdata == NULL || datafile->offpulse_rms == NULL || datafile->tsamp_list == NULL) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR make_paswing_fromIQUV: Memory allocation error.");
    return 0;
  }
  for(j = 0; j < (datafile->NrBins); j++) {
    datafile->tsamp_list[j] = get_pulse_longitude(*datafile, 0, j, verbose);
    datafile->tsamp_list[j] += loffset;
  }
  if(normalize && (datafile->NrSubints > 1 || datafile->NrFreqChan > 1)) {
    fflush(stdout);
    printwarning(verbose.debug, "WARNING make_paswing_fromIQUV: Normalization will cause all subintegrations/frequency channels to be normalised individually. This may not be desired.");
  }
  long sindex_I, sindex_Q, sindex_U, sindex_V, newindex_I, newindex_L, newindex_V, newindex_Pa, newindex_dPa;
  for(pulsenr = 0; pulsenr < datafile->NrSubints; pulsenr++) {
    for(freqnr = 0; freqnr < datafile->NrFreqChan; freqnr++) {
      sindex_I = datafile->NrBins*(0+datafile->NrPols*(freqnr+pulsenr*datafile->NrFreqChan));
      sindex_Q = datafile->NrBins*(1+datafile->NrPols*(freqnr+pulsenr*datafile->NrFreqChan));
      sindex_U = datafile->NrBins*(2+datafile->NrPols*(freqnr+pulsenr*datafile->NrFreqChan));
      sindex_V = datafile->NrBins*(3+datafile->NrPols*(freqnr+pulsenr*datafile->NrFreqChan));
      newindex_I = datafile->NrBins*(0+5*(freqnr+datafile->NrFreqChan*pulsenr));
      newindex_L = datafile->NrBins*(1+5*(freqnr+datafile->NrFreqChan*pulsenr));
      newindex_V = datafile->NrBins*(2+5*(freqnr+datafile->NrFreqChan*pulsenr));
      newindex_Pa = datafile->NrBins*(3+5*(freqnr+datafile->NrFreqChan*pulsenr));
      newindex_dPa = datafile->NrBins*(4+5*(freqnr+datafile->NrFreqChan*pulsenr));
      if(normalize == 0) {
 ymax = 1;
      }else {
 ymax = datafile->data[sindex_I];
 for(j = 1; j < (datafile->NrBins); j++) {
   if(datafile->data[sindex_I+j] > ymax)
     ymax = datafile->data[sindex_I+j];
 }
      }
      if(ymax == 0)
 ymax = 1;
      for(j = 0; j < (datafile->NrBins); j++) {
 datafile->data[sindex_I + j] /= ymax;
 datafile->data[sindex_Q + j] /= correctQV*ymax;
 datafile->data[sindex_U + j] /= ymax;
 datafile->data[sindex_V + j] /= correctV*correctQV*ymax;
 newdata[j+newindex_L] = sqrt((datafile->data[sindex_Q+j])*(datafile->data[sindex_Q+j])+(datafile->data[sindex_U+j])*(datafile->data[sindex_U+j]));
 if(Ppulse != NULL) {
   Ppulse[j] = sqrt(newdata[j+newindex_L]*newdata[j+newindex_L]+(datafile->data[sindex_V+j])*(datafile->data[sindex_V+j]));
 }
      }
      I = 0;
      datafile->offpulse_rms[0+5*(freqnr + datafile->NrFreqChan*pulsenr)] = 0;
      datafile->offpulse_rms[1+5*(freqnr + datafile->NrFreqChan*pulsenr)] = 0;
      datafile->offpulse_rms[2+5*(freqnr + datafile->NrFreqChan*pulsenr)] = 0;
      datafile->offpulse_rms[3+5*(freqnr + datafile->NrFreqChan*pulsenr)] = -1;
      datafile->offpulse_rms[4+5*(freqnr + datafile->NrFreqChan*pulsenr)] = -1;
      RMSQ = 0;
      RMSU = 0;
      RMSP = 0;
      NrOffpulseBins = 0;
      for(i = 0; i < (datafile->NrBins); i++) {
 Loffpulse[i] = 0;
 if(checkRegions(i, &onpulse, 0, verbose) == 0) {
   NrOffpulseBins++;
   I += datafile->data[datafile->NrBins*(0+datafile->NrPols*(freqnr+pulsenr*datafile->NrFreqChan))+i];
   datafile->offpulse_rms[0+5*(freqnr + datafile->NrFreqChan*pulsenr)] += (datafile->data[sindex_I+i])*(datafile->data[sindex_I+i]);
   RMSQ += (datafile->data[sindex_Q+i])*(datafile->data[sindex_Q+i]);
   RMSU += (datafile->data[sindex_U+i])*(datafile->data[sindex_U+i]);
   datafile->offpulse_rms[1+5*(freqnr + datafile->NrFreqChan*pulsenr)] += (newdata[i+newindex_L])*(newdata[i+newindex_L]);
   datafile->offpulse_rms[2+5*(freqnr + datafile->NrFreqChan*pulsenr)] += (datafile->data[sindex_V+i])*(datafile->data[sindex_V+i]);
   Loffpulse[NrOffpulseBins-1] = newdata[i+newindex_L];
   if(Ppulse != NULL)
     Poffpulse[NrOffpulseBins-1] = Ppulse[i];
 }
      }
      I /= (float)NrOffpulseBins;
      datafile->offpulse_rms[0+5*(freqnr + datafile->NrFreqChan*pulsenr)] = sqrt(datafile->offpulse_rms[0+5*(freqnr + datafile->NrFreqChan*pulsenr)])/(float)sqrt(NrOffpulseBins);
      RMSQ = sqrt(RMSQ)/(float)sqrt(NrOffpulseBins);
      RMSU = sqrt(RMSU)/(float)sqrt(NrOffpulseBins);
      datafile->offpulse_rms[1+5*(freqnr + datafile->NrFreqChan*pulsenr)] = sqrt(datafile->offpulse_rms[1+5*(freqnr + datafile->NrFreqChan*pulsenr)])/(float)sqrt(NrOffpulseBins);
      datafile->offpulse_rms[2+5*(freqnr + datafile->NrFreqChan*pulsenr)] = sqrt(datafile->offpulse_rms[2+5*(freqnr + datafile->NrFreqChan*pulsenr)])/(float)sqrt(NrOffpulseBins);
      if(verbose.verbose) {
 if((freqnr == 0 && pulsenr == 0) || verbose.debug) {
   for(indent = 0; indent < verbose.indent; indent++) printf(" ");
   fprintf(stdout, "  PA conversion output for subint %ld frequency channel %ld:\n", pulsenr, freqnr);
   for(indent = 0; indent < verbose.indent; indent++) printf(" ");
   fprintf(stdout, "    Averige baseline Stokes I: %f\n", I);
   for(indent = 0; indent < verbose.indent; indent++) printf(" ");
   fprintf(stdout, "    RMS I: %f\n", datafile->offpulse_rms[0+5*(freqnr + datafile->NrFreqChan*pulsenr)]);
   for(indent = 0; indent < verbose.indent; indent++) printf(" ");
   fprintf(stdout, "    RMS Q: %f\n", RMSQ);
   for(indent = 0; indent < verbose.indent; indent++) printf(" ");
   fprintf(stdout, "    RMS U: %f\n", RMSU);
   for(indent = 0; indent < verbose.indent; indent++) printf(" ");
   fprintf(stdout, "    RMS V: %f\n", datafile->offpulse_rms[2+5*(freqnr + datafile->NrFreqChan*pulsenr)]);
   for(indent = 0; indent < verbose.indent; indent++) printf(" ");
   fprintf(stdout, "    RMS L: %f\n", datafile->offpulse_rms[1+5*(freqnr + datafile->NrFreqChan*pulsenr)]);
 }
      }
      gsl_sort_float (Loffpulse, 1, NrOffpulseBins);
      medianL = gsl_stats_float_median_from_sorted_data(Loffpulse, 1, NrOffpulseBins);
      fflush(stdout);
      if((verbose.verbose && freqnr == 0 && pulsenr == 0) || verbose.debug) {
 for(indent = 0; indent < verbose.indent; indent++) printf(" ");
 fprintf(stdout, "    Median L: %f\n", medianL);
      }
      if(Ppulse != NULL) {
 gsl_sort_float (Poffpulse, 1, NrOffpulseBins);
 medianP = gsl_stats_float_median_from_sorted_data(Poffpulse, 1, NrOffpulseBins);
 fflush(stdout);
 if((verbose.verbose && freqnr == 0 && pulsenr == 0) || verbose.debug) {
   for(indent = 0; indent < verbose.indent; indent++) printf(" ");
   fprintf(stdout, "    Median sqrt(L^2+V^2): %f\n", medianP);
 }
      }
      for(j = 0; j < (datafile->NrBins); j++) {
 if(correctLbias == 1) {
   float junk = (0.5*(RMSQ+RMSU)/newdata[j+newindex_L]);
   if(junk < 1)
     newdata[j+newindex_L] *= sqrt(1.0-junk*junk);
   else
     newdata[j+newindex_L] = 0;
 }else if(correctLbias == 0) {
   newdata[j+newindex_L] -= medianL;
 }
 if(Ppulse != NULL)
   Ppulse[j] -= medianP;
      }
      for(i = 0; i < (datafile->NrBins); i++) {
 newdata[i+newindex_Pa] = 90.0*atan2(datafile->data[sindex_U+i],datafile->data[sindex_Q+i])/M_PI;
 if(datafile->data[i+sindex_Q] != 0 || datafile->data[i+sindex_U] != 0) {
   newdata[i+newindex_dPa] = sqrt((datafile->data[sindex_Q+i]*RMSU)*(datafile->data[sindex_Q+i]*RMSU) + (datafile->data[sindex_U+i]*RMSQ)*(datafile->data[sindex_U+i]*RMSQ));
   newdata[i+newindex_dPa] /= 2.0*(datafile->data[i+sindex_Q]*datafile->data[i+sindex_Q] + datafile->data[i+sindex_U]*datafile->data[i+sindex_U]);
   newdata[i+newindex_dPa] *= 180.0/M_PI;
 }else {
   newdata[i+newindex_dPa] = 0;
 }
      }
      for(j = 0; j < (datafile->NrBins); j++) {
 newdata[j+newindex_I] = datafile->data[j+sindex_I];
 newdata[j+newindex_V] = datafile->data[j+sindex_V];
      }
    }
  }
  free(datafile->data);
  datafile->data = newdata;
  datafile->tsampMode = TSAMPMODE_LONGITUDELIST;
  datafile->NrPols = 5;
  datafile->poltype = POLTYPE_ILVPAdPA;
  free(Loffpulse);
  free(Poffpulse);
  return 1;
}
int writePPOLHeader(datafile_definition datafile, int argc, char **argv, verbose_definition verbose)
{
  char *txt;
  txt = malloc(10000);
  if(txt == NULL) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR writePPOLHeader: Memory allocation error.");
    return 0;
  }
  constructCommandLineString(txt, 10000, argc, argv, verbose);
  fprintf(datafile.fptr_hdr, "#ppol file: %s\n", txt);
  free(txt);
  return 1;
}
int readPPOLHeader(datafile_definition *datafile, int extended, verbose_definition verbose)
{
  float dummy_float;
  int ret, maxlinelength, nrwords;
  char *txt, *ret_ptr, *word_ptr;
  maxlinelength = 2000;
  txt = malloc(maxlinelength);
  if(txt == NULL) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR readPPOLHeader: Memory allocation error.");
    return 0;
  }
  datafile->isFolded = 1;
  datafile->foldMode = FOLDMODE_FIXEDPERIOD;
  datafile->fixedPeriod = 0;
  datafile->tsampMode = TSAMPMODE_LONGITUDELIST;
  datafile->fixedtsamp = 0;
  datafile->tsubMode = TSUBMODE_FIXEDTSUB;
  if(datafile->tsub_list != NULL)
    free(datafile->tsub_list);
  datafile->tsub_list = (double *)malloc(sizeof(double));
  if(datafile->tsub_list == NULL) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR readPPOLHeader: Memory allocation error");
    return 0;
  }
  datafile->tsub_list[0] = 0;
  datafile->NrSubints = 1;
  datafile->NrFreqChan = 1;
  datafile->datastart = 0;
  if(extended) {
    datafile->poltype = POLTYPE_ILVPAdPA;
    datafile->NrPols = 5;
  }else {
    datafile->poltype = POLTYPE_PAdPA;
    datafile->NrPols = 2;
  }
  rewind(datafile->fptr);
  ret = fread(txt, 1, 3, datafile->fptr);
  txt[3] = 0;
  if(ret != 3) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR readPPOLHeader: cannot read from file.");
    free(txt);
    return 0;
  }
  if(strcmp(txt, "#pp") != 0
) {
    fflush(stdout);
    printwarning(verbose.debug, "WARNING readPPOLHeader: File does not appear to be in PPOL or PPOLSHORT format. I will try to load file, but this will probably fail. Did you run ppol first?");
  }
  skipallhashedlines(datafile);
  datafile->NrBins = 0;
  dummy_float = 0;
  do {
    ret_ptr = fgets(txt, maxlinelength, datafile->fptr);
    if(ret_ptr != NULL) {
      if(txt[0] != '#') {
 if(extended) {
   word_ptr = pickWordFromString(txt, 2, &nrwords, 1, ' ', verbose);
   if(nrwords != 10) {
     fflush(stdout);
     printerror(verbose.debug, "ERROR readPPOLHeader: Line should have 10 words, got %d", nrwords);
     if(nrwords == 3)
       printerror(verbose.debug, "                             Maybe file is in format %s?", returnFileFormat_str(PPOL_SHORT_format));
     printerror(verbose.debug, "                             Line: '%s'.", txt);
     free(txt);
     return 0;
   }
 }else {
   word_ptr = pickWordFromString(txt, 1, &nrwords, 1, ' ', verbose);
   if(nrwords != 3) {
     fflush(stdout);
     printerror(verbose.debug, "ERROR readPPOLHeader: Line should have 3 words, got %d", nrwords);
     if(nrwords == 10)
       printerror(verbose.debug, "                             Maybe file is in format %s?", returnFileFormat_str(PPOL_format));
     printerror(verbose.debug, "                             Line: '%s'.", txt);
     free(txt);
     return 0;
   }
 }
 ret = sscanf(word_ptr, "%f", &dummy_float);
 if(ret != 1) {
   fflush(stdout);
   printerror(verbose.debug, "ERROR readPPOLHeader: Cannot interpret as a float: '%s'.", txt);
   free(txt);
   return 0;
 }
 if(dummy_float >= 360) {
   fflush(stdout);
   printwarning(verbose.debug, "WARNING: IGNORING POINTS AT PULSE LONGITUDES > 360 deg.");
 }else {
   (datafile->NrBins)++;
 }
      }
    }
  }while(ret_ptr != NULL && dummy_float < 360);
  fflush(stdout);
  if(verbose.verbose) fprintf(stdout, "Going to load %ld points from %s\n", datafile->NrBins, datafile->filename);
  if(datafile->NrBins == 0) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR readPPOLHeader: No data in %s", datafile->filename);
    free(txt);
    return 0;
  }
  fseek(datafile->fptr, datafile->datastart, SEEK_SET);
  free(txt);
  if(datafile->offpulse_rms != NULL) {
    free(datafile->offpulse_rms);
    datafile->offpulse_rms = NULL;
  }
  if(extended) {
    datafile->offpulse_rms = (float *)malloc(datafile->NrSubints*datafile->NrFreqChan*datafile->NrPols*sizeof(float));
    if(datafile->offpulse_rms == NULL) {
      fflush(stdout);
      printerror(verbose.debug, "ERROR readPPOLHeader: Memory allocation error");
      return 0;
    }
  }
  datafile->tsamp_list = (double *)malloc(datafile->NrBins*sizeof(double));
  if(datafile->tsamp_list == NULL) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR readPPOLHeader: Memory allocation error");
    return 0;
  }
  return 1;
}
int readPPOLfile(datafile_definition *datafile, float *data, int extended, float add_longitude_shift, verbose_definition verbose)
{
  int maxlinelength;
  long i, k, dummy_long;
  char *txt, *ret_ptr;
  if(datafile->NrBins == 0) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR readPPOLfile: No data in %s", datafile->filename);
    return 0;
  }
  maxlinelength = 2000;
  txt = malloc(maxlinelength);
  if(txt == NULL) {
    fflush(stdout);
    printerror(verbose.debug, "ERROR readPPOLfile: Memory allocation error.");
    return 0;
  }
  fseek(datafile->fptr, datafile->datastart, SEEK_SET);
  k = 0;
  if(extended) {
    datafile->offpulse_rms[3] = -1;
    datafile->offpulse_rms[4] = -1;
  }
  for(i = 0; i < datafile->NrBins; i++) {
    ret_ptr = fgets(txt, maxlinelength, datafile->fptr);
    if(ret_ptr == NULL) {
      fflush(stdout);
      printerror(verbose.debug, "ERROR readPPOLfile: Cannot read next line, should not happen after successfully reading in header");
      free(txt);
      return 0;
    }
    if(txt[0] != '#') {
      if(extended == 0) {
 sscanf(txt, "%lf %f %f", &(datafile->tsamp_list[k]), &(data[k]), &(data[k+datafile->NrBins]));
      }else {
 sscanf(txt, "%ld %lf %f %f %f %f %f %f %f %f", &dummy_long, &(datafile->tsamp_list[k]), &(data[k]), &(datafile->offpulse_rms[0]), &(data[k+datafile->NrBins]), &(datafile->offpulse_rms[1]), &(data[k+2*datafile->NrBins]), &(datafile->offpulse_rms[2]), &(data[k+3*datafile->NrBins]), &(data[k+4*datafile->NrBins]));
      }
      datafile->tsamp_list[k] += add_longitude_shift;
      if(datafile->tsamp_list[k] >= 0 && datafile->tsamp_list[k] < 360) {
   k++;
      }else {
 fflush(stdout);
 printwarning(verbose.debug, "WARNING readPPOLfile: IGNORING POINTS AT PULSE LONGITUDES > 360 deg.");
      }
    }
  }
  if(k != datafile->NrBins) {
    fflush(stdout);
    printerror(verbose.debug, "WARNING readPPOLfile: The nr of bins read in is different as determined from header. Something is wrong.");
    return 0;
  }
  fflush(stdout);
  if(verbose.verbose) fprintf(stdout, "readPPOLfile: Accepted %ld points\n", datafile->NrBins);
     free(txt);
  return 1;
}
int writePPOLfile(datafile_definition datafile, float *data, int extended, int onlysignificantPA, int twoprofiles, float PAoffset, verbose_definition verbose)
{
  long j;
  if(datafile.poltype != POLTYPE_ILVPAdPA && datafile.poltype != POLTYPE_PAdPA) {
    printerror(verbose.debug, "ERROR writePPOLfile: Data doesn't appear to have poltype ILVPAdPA or PAdPA (it is %d).", datafile.poltype);
    return 0;
  }
  if(datafile.poltype == POLTYPE_ILVPAdPA && datafile.NrPols != 5) {
    printerror(verbose.debug, "ERROR writePPOLfile: 5 polarization channels were expected, but there are only %ld.", datafile.NrPols);
    return 0;
  }else if(datafile.poltype == POLTYPE_PAdPA && datafile.NrPols != 2) {
    printerror(verbose.debug, "ERROR writePPOLfile: 2 polarization channels were expected, but there are only %ld.", datafile.NrPols);
    return 0;
  }
  if(datafile.NrSubints > 1 || datafile.NrFreqChan > 1) {
    printerror(verbose.debug, "ERROR writePPOLfile: Can only do this opperation if there is one subint and one frequency channel.");
    return 0;
  }
  if(datafile.tsampMode != TSAMPMODE_LONGITUDELIST) {
    printerror(verbose.debug, "ERROR writePPOLfile: Expected pulse longitudes to be defined.");
    return 0;
  }
  int pa_offset, dpa_offset;
  if(datafile.poltype == POLTYPE_ILVPAdPA) {
    pa_offset = 3;
    dpa_offset = 4;
  }
  if(datafile.poltype == POLTYPE_PAdPA) {
    pa_offset = 0;
    dpa_offset = 1;
  }
  for(j = 0; j < datafile.NrBins; j++) {
    if(data[j+dpa_offset*datafile.NrBins] > 0 || onlysignificantPA == 0) {
      if(extended) {
 fprintf(datafile.fptr, "%ld %e %e %e %e %e %e %e %e %e\n", j, datafile.tsamp_list[j], data[j], datafile.offpulse_rms[0], data[j+datafile.NrBins], datafile.offpulse_rms[1], data[j+2*datafile.NrBins], datafile.offpulse_rms[2], data[j+pa_offset*datafile.NrBins]+PAoffset, data[j+dpa_offset*datafile.NrBins]);
      }else {
 fprintf(datafile.fptr, "%e %e %e\n", datafile.tsamp_list[j], data[j+pa_offset*datafile.NrBins]+PAoffset, data[j+dpa_offset*datafile.NrBins]);
      }
    }
  }
  if(twoprofiles) {
    for(j = 0; j < datafile.NrBins; j++) {
      if(data[j+dpa_offset*datafile.NrBins] > 0 || onlysignificantPA == 0) {
 if(extended) {
   fprintf(datafile.fptr, "%ld %e %e %e %e %e %e %e %e %e\n", j+datafile.NrBins, datafile.tsamp_list[j]+360, data[j], datafile.offpulse_rms[0], data[j+datafile.NrBins], datafile.offpulse_rms[1], data[j+2*datafile.NrBins], datafile.offpulse_rms[2], data[j+3*datafile.NrBins]+PAoffset, data[j+dpa_offset*datafile.NrBins]);
 }else {
   fprintf(datafile.fptr, "%e %e %e\n", datafile.tsamp_list[j]+360, data[j]+PAoffset, data[j+dpa_offset*datafile.NrBins]);
 }
      }
    }
  }
  return 1;
}
