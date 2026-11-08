@echo off
rem Builds third_party sources (stb_vorbis, libogg, libopus decoder, opusfile) into build\tp\tlap_tp.lib.
rem Called from build.bat when the lib is missing (or "build.bat tp"). Only the .c files listed here are compiled.
rem Nothing inside the downloaded trees (configure scripts, tests, executables) is run.
setlocal
cd /d "%~dp0.."
set TP=third_party
if not exist build\tp mkdir build\tp
del /q build\tp\*.obj build\tp\tlap_tp.lib 2>nul
set OPUS=%TP%\opus-1.6.1
set CL_C=/nologo /c /TC /O1 /MT /Gy /W0 /utf-8 /Fo:build\tp\
set INC_OPUS=/I%TP%\tlap_cfg /I%OPUS%\include /I%OPUS%\celt /I%OPUS%\silk /I%OPUS%\silk\float /I%OPUS%
set DEF_OPUS=/DHAVE_CONFIG_H /DOPUS_BUILD /DUSE_ALLOCA

cl %CL_C% /I%TP%\stb_vorbis-1.22 /DSTB_VORBIS_NO_PUSHDATA_API %TP%\stb_vorbis-1.22\stb_vorbis.c || exit /b 1
cl %CL_C% /I%TP%\tlap_cfg /I%TP%\libogg-1.3.6\include %TP%\libogg-1.3.6\src\bitwise.c %TP%\libogg-1.3.6\src\framing.c || exit /b 1
cl %CL_C% %INC_OPUS% %DEF_OPUS% ^
  %OPUS%\celt\bands.c %OPUS%\celt\celt.c %OPUS%\celt\celt_decoder.c %OPUS%\celt\cwrs.c %OPUS%\celt\entcode.c %OPUS%\celt\entdec.c %OPUS%\celt\entenc.c ^
  %OPUS%\celt\kiss_fft.c %OPUS%\celt\laplace.c %OPUS%\celt\mathops.c %OPUS%\celt\mdct.c %OPUS%\celt\modes.c %OPUS%\celt\pitch.c %OPUS%\celt\celt_lpc.c ^
  %OPUS%\celt\quant_bands.c %OPUS%\celt\rate.c %OPUS%\celt\vq.c || exit /b 1
cl %CL_C% %INC_OPUS% %DEF_OPUS% ^
  %OPUS%\silk\CNG.c %OPUS%\silk\code_signs.c %OPUS%\silk\init_decoder.c %OPUS%\silk\decode_core.c %OPUS%\silk\decode_frame.c %OPUS%\silk\decode_parameters.c ^
  %OPUS%\silk\decode_indices.c %OPUS%\silk\decode_pulses.c %OPUS%\silk\decoder_set_fs.c %OPUS%\silk\dec_API.c %OPUS%\silk\gain_quant.c %OPUS%\silk\interpolate.c ^
  %OPUS%\silk\NLSF_decode.c %OPUS%\silk\PLC.c %OPUS%\silk\shell_coder.c %OPUS%\silk\tables_gain.c %OPUS%\silk\tables_LTP.c %OPUS%\silk\tables_NLSF_CB_NB_MB.c ^
  %OPUS%\silk\tables_NLSF_CB_WB.c %OPUS%\silk\tables_other.c %OPUS%\silk\tables_pitch_lag.c %OPUS%\silk\tables_pulses_per_block.c %OPUS%\silk\bwexpander_32.c ^
  %OPUS%\silk\bwexpander.c %OPUS%\silk\debug.c %OPUS%\silk\decode_pitch.c %OPUS%\silk\inner_prod_aligned.c %OPUS%\silk\lin2log.c %OPUS%\silk\log2lin.c ^
  %OPUS%\silk\LPC_analysis_filter.c %OPUS%\silk\LPC_inv_pred_gain.c %OPUS%\silk\table_LSF_cos.c %OPUS%\silk\NLSF2A.c %OPUS%\silk\NLSF_stabilize.c ^
  %OPUS%\silk\NLSF_unpack.c %OPUS%\silk\NLSF_VQ_weights_laroia.c %OPUS%\silk\pitch_est_tables.c %OPUS%\silk\resampler.c %OPUS%\silk\resampler_down2_3.c ^
  %OPUS%\silk\resampler_down2.c %OPUS%\silk\resampler_private_AR2.c %OPUS%\silk\resampler_private_down_FIR.c %OPUS%\silk\resampler_private_IIR_FIR.c ^
  %OPUS%\silk\resampler_private_up2_HQ.c %OPUS%\silk\resampler_rom.c %OPUS%\silk\sigm_Q15.c %OPUS%\silk\sort.c %OPUS%\silk\sum_sqr_shift.c ^
  %OPUS%\silk\stereo_decode_pred.c %OPUS%\silk\stereo_MS_to_LR.c %OPUS%\silk\biquad_alt.c %OPUS%\silk\LPC_fit.c %OPUS%\silk\A2NLSF.c || exit /b 1
cl %CL_C% %INC_OPUS% %DEF_OPUS% ^
  %OPUS%\src\opus.c %OPUS%\src\opus_decoder.c %OPUS%\src\extensions.c %OPUS%\src\opus_multistream.c %OPUS%\src\opus_multistream_decoder.c %OPUS%\src\repacketizer.c || exit /b 1
cl %CL_C% /I%TP%\tlap_cfg /I%TP%\libogg-1.3.6\include /I%OPUS%\include /I%TP%\opusfile-0.12\include ^
  %TP%\opusfile-0.12\src\opusfile.c %TP%\opusfile-0.12\src\internal.c %TP%\opusfile-0.12\src\info.c %TP%\opusfile-0.12\src\stream.c || exit /b 1
lib /nologo /OUT:build\tp\tlap_tp.lib build\tp\*.obj || exit /b 1
endlocal
