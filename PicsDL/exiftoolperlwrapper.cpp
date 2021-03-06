/**
 * Copyright 2014-2015 Clément Nardi
 *
 * This file is part of PicsDL.
 *
 * PicsDL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PicsDL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PicsDL.  If not, see <http://www.gnu.org/licenses/>.
 *
 **/

#include "exiftoolperlwrapper.h"

#include "EXTERN.h"
#include "perl.h"

EXTERN_C void boot_DynaLoader (pTHX_ CV* cv);
EXTERN_C void
xs_init(pTHX)
{
    const char *file = __FILE__;
    /* DynaLoader is a special case */
    newXS("DynaLoader::boot_DynaLoader", boot_DynaLoader, file);
}


static int argc = 3;
static char* argv[3] = {(char *)"perl", (char *)"-e" , (char *)"0"};
static char** env = NULL;
static bool globalInitDone = false;


ExifToolPerlWrapper::ExifToolPerlWrapper(const char *includeDirs_) {
    if (!globalInitDone) {
        globalInitDone = true;
        PERL_SYS_INIT3(&argc,(char ***)&argv,&env);
    }
    my_perl = perl_alloc();

    PERL_SET_CONTEXT(my_perl);

    perl_construct(my_perl);
    PL_exit_flags |= PERL_EXIT_DESTRUCT_END;
    perl_parse(my_perl, xs_init, argc, argv, env);
    perl_run(my_perl);

    char * includeDirs = new char [strlen(includeDirs_)+1];
    strcpy(includeDirs,includeDirs_);
    char * includeDir = strtok(includeDirs,",");
    while (includeDir != NULL) {
        SV* includeDir_SV = get_sv("includeDir", GV_ADD);
        sv_setpv(includeDir_SV, includeDir);
        eval_pv("BEGIN { push @INC, $includeDir }", TRUE);
        includeDir = strtok(NULL,",");
    }
    eval_pv("use Image::ExifTool qw(:Public);", TRUE);
    eval_pv("use Data::Dumper;", TRUE);
    eval_pv("use IO::Scalar;", TRUE);

    eval_pv("$exifTool = new Image::ExifTool;", TRUE);
    //eval_pv("$exifTool->Options(CharsetFileName => 'UTF8');", TRUE);

}

ExifToolPerlWrapper::~ExifToolPerlWrapper(){
    PERL_SET_CONTEXT(my_perl);
    perl_destruct(my_perl);
    perl_free(my_perl);
}

/* this function doesn't work when the path contains utf8 characters */
void ExifToolPerlWrapper::loadTrackFile(const char * trackFilePath) {
    PERL_SET_CONTEXT(my_perl);
    SV* trackFilePath_SV = get_sv("trackFilePath", GV_ADD);
    sv_setpv(trackFilePath_SV, trackFilePath);
    eval_pv("print {STDERR} \"$trackFilePath\n\";", TRUE);
    eval_pv("my @loadTrackResults = $exifTool->SetNewValue('Geotag', $trackFilePath);", TRUE);
    eval_pv("print {STDERR} Dumper(@loadTrackResults);", TRUE);
}

void ExifToolPerlWrapper::loadTrackContent(const char * trackFileContent, long size) {
    PERL_SET_CONTEXT(my_perl);
    SV* trackFilePath_SV = get_sv("trackFileContent", GV_ADD);
    sv_setpvn(trackFilePath_SV, trackFileContent, size);
    fprintf(stderr,"loading track file content: size=%d\n",size);fflush(stderr);
    eval_pv("my @loadTrackResults = $exifTool->SetNewValue('Geotag', $trackFileContent);", TRUE);
    eval_pv("print {STDERR} 'loadTrackContent: ' . Dumper(@loadTrackResults) . '\n';", TRUE);
}

void ExifToolPerlWrapper::geotag(char *in, long in_size, char **out, long *out_size){

    PERL_SET_CONTEXT(my_perl);

    SV* source = get_sv("sourceFileData", GV_ADD);
    sv_setpvn(source, in, in_size);

    eval_pv("my $destContent = '';  #init buffer", TRUE);
    eval_pv("open $sourceHandle, \"<\", \\$sourceFileData;", TRUE);
    eval_pv("open $destHandle, \">\", \\$destContent;", TRUE);

    eval_pv("$exifTool->ExtractInfo($sourceHandle);", TRUE);

    //eval_pv("print {STDERR} 'ExtractInfo error: ' . Dumper($exifTool->GetValue('Error')) . '\n';", TRUE);
    //eval_pv("print {STDERR} 'ExtractInfo warning: ' . Dumper($exifTool->GetValue('Warning')) . '\n';", TRUE);
    eval_pv("print {STDERR} 'DateTimeOriginal ' . Dumper($exifTool->GetValue('DateTimeOriginal', 'PrintConv')) . '\n';", TRUE);
    eval_pv("print {STDERR} 'GPSLatitude ' . Dumper($exifTool->GetValue('GPSLatitude')) . '\n';", TRUE);

    eval_pv("$exifTool->SetNewValue('Geotime', $exifTool->GetValue('DateTimeOriginal'));", TRUE);
    //eval_pv("print {STDERR} 'SetNewValue error: ' . Dumper($exifTool->GetValue('Error')) . '\n';", TRUE);
    //eval_pv("print {STDERR} 'SetNewValue warning: ' . Dumper($exifTool->GetValue('Warning')) . '\n';", TRUE);

    eval_pv("$exifTool->WriteInfo($sourceHandle, $destHandle);", TRUE);
    //eval_pv("print {STDERR} 'WriteInfo error: ' . Dumper($exifTool->GetValue('Error')) . '\n';", TRUE);
    //eval_pv("print {STDERR} 'WriteInfo warning: ' . Dumper($exifTool->GetValue('Warning')) . '\n';", TRUE);

    //eval_pv("print {STDERR} 'sourceFileData: ' . Dumper(substr($sourceFileData,0,100)) . '\n';", TRUE);
    //eval_pv("print {STDERR} 'destContent: ' . Dumper(substr($destContent,0,100)) . '\n';", TRUE);

    STRLEN len;
    char * res = SvPV(get_sv("destContent", 0), len);
    //fprintf(stderr,"sending back a string of size %d: ",len,res);
    //fwrite(res,1,100,stderr);fprintf(stderr,"\n");fflush(stderr);

    *out_size = len;
    *out = res;
}


/* alpha version, doesn't work.
 Perl hangs with
panic: restartop
*/
void ExifToolPerlWrapper::geotagPipe(char *in, long in_size, char **out, long *out_size){

    PERL_SET_CONTEXT(my_perl);

    SV* source = get_sv("sourceFileData", GV_ADD);
    sv_setpvn(source, in, in_size);

    eval_pv("my $destContent = '';  #init buffer", TRUE);
    eval_pv("open $sourceHandle, \"<\", \\$sourceFileData;", TRUE);

    eval_pv("use IO::Pipe;", TRUE);
    eval_pv("$pipeOut = IO::Pipe->new();", TRUE);


    eval_pv("if($pid = fork()) {\
    $pipeOut->reader();\
    while(<$pipeOut>) {\
        print {STDERR} 'a';\
    }\
} elsif(defined $pid) {\
    $pipe->writer();\
    $exifTool->ExtractInfo($sourceHandle);\
    $exifTool->SetNewValue('Geotime', $exifTool->GetValue('DateTimeOriginal'));\
    $exifTool->WriteInfo($sourceHandle, $pipeOut);\
}", TRUE);
    fflush(stderr);

    *out = NULL;
    *out_size = 0;

}


/* Here are the additional tags generated by exiftool (output of libexif)
Dumping IFD 'GPS'...
Dumping exif content (9 entries)...
  Tag: 0x0 ('GPSVersionID')
    Format: 1 ('Byte')
    Components: 4
    Size: 4
    Value: 2.3.0.0
  Tag: 0x1 ('GPSLatitudeRef')
    Format: 2 ('ASCII')
    Components: 2
    Size: 2
    Value: N
  Tag: 0x2 ('GPSLatitude')
    Format: 5 ('Rational')
    Components: 3
    Size: 24
    Value: 43, 38, 57.3228
  Tag: 0x3 ('GPSLongitudeRef')
    Format: 2 ('ASCII')
    Components: 2
    Size: 2
    Value: E
  Tag: 0x4 ('GPSLongitude')
    Format: 5 ('Rational')
    Components: 3
    Size: 24
    Value:  6, 55, 19.0191
  Tag: 0x5 ('GPSAltitudeRef')
    Format: 1 ('Byte')
    Components: 1
    Size: 1
    Value: Sea level
  Tag: 0x6 ('GPSAltitude')
    Format: 5 ('Rational')
    Components: 1
    Size: 8
    Value:  0
  Tag: 0x7 ('GPSTimeStamp')
    Format: 5 ('Rational')
    Components: 3
    Size: 24
    Value: 10:57:36.00
  Tag: 0x1d ('GPSDateStamp')
    Format: 2 ('ASCII')
    Components: 11
    Size: 11
    Value: 2015:01:24
*/



void ExifToolPerlWrapper::getGeotags(const char *geotime, int *nbTags,  char keys[10][200], char values[10][200]) {

    PERL_SET_CONTEXT(my_perl);

    SV* geotimeStr = get_sv("geotime", GV_ADD);
    sv_setpv(geotimeStr, geotime);

    fprintf(stderr,"geotime = %s\n",geotime);fflush(stderr);

    eval_pv("$exifTool->SetNewValue('Geotime', $geotime);", TRUE);
    //eval_pv("print {STDERR} 'Count ' . Dumper($exifTool->CountNewValues())", TRUE);
    *nbTags = 0;
    char * value = NULL;

    /* GPSVersionID is not set here, but is written to the file...
    eval_pv("$value = $exifTool->GetNewValues('GPSVersionID')", TRUE);
    value = SvPV_nolen(get_sv("value", 0));*/
    strcpy(keys[*nbTags],"GPSVersionID");
    strcpy(values[(*nbTags)++],"2.3.0.0");

    eval_pv("$value = $exifTool->GetNewValues('GPSLatitudeRef')", TRUE);
    value = SvPV_nolen(get_sv("value", 0));
    strcpy(keys[*nbTags],"GPSLatitudeRef");
    strcpy(values[(*nbTags)++],value);

    eval_pv("$value = $exifTool->GetNewValues('GPSLatitude')", TRUE);
    value = SvPV_nolen(get_sv("value", 0));
    strcpy(keys[*nbTags],"GPSLatitude");
    strcpy(values[(*nbTags)++],value);

    eval_pv("$value = $exifTool->GetNewValues('GPSLongitudeRef')", TRUE);
    value = SvPV_nolen(get_sv("value", 0));
    strcpy(keys[*nbTags],"GPSLongitudeRef");
    strcpy(values[(*nbTags)++],value);

    eval_pv("$value = $exifTool->GetNewValues('GPSLongitude')", TRUE);
    value = SvPV_nolen(get_sv("value", 0));
    strcpy(keys[*nbTags],"GPSLongitude");
    strcpy(values[(*nbTags)++],value);

    eval_pv("$value = $exifTool->GetNewValues('GPSAltitudeRef')", TRUE);
    value = SvPV_nolen(get_sv("value", 0));
    strcpy(keys[*nbTags],"GPSAltitudeRef");
    strcpy(values[(*nbTags)++],value);

    eval_pv("$value = $exifTool->GetNewValues('GPSAltitude')", TRUE);
    value = SvPV_nolen(get_sv("value", 0));
    strcpy(keys[*nbTags],"GPSAltitude");
    strcpy(values[(*nbTags)++],value);

    eval_pv("$value = $exifTool->GetNewValues('GPSTimeStamp')", TRUE);
    value = SvPV_nolen(get_sv("value", 0));
    strcpy(keys[*nbTags],"GPSTimeStamp");
    strcpy(values[(*nbTags)++],value);

    eval_pv("$value = $exifTool->GetNewValues('GPSDateStamp')", TRUE);
    value = SvPV_nolen(get_sv("value", 0));
    strcpy(keys[*nbTags],"GPSDateStamp");
    strcpy(values[(*nbTags)++],value);

    //eval_pv("print {STDERR} 'GPSLongitude ' . Dumper($exifTool->GetNewValues('GPSLongitude'))", TRUE);
    //eval_pv("print {STDERR} 'GPSAltitude ' . Dumper($exifTool->GetNewValues('GPSAltitude'))", TRUE);
}


/**
 * @brief exitPerl
 * exits Perl properly. To be called only once when closing application.
 */
void ExifToolPerlWrapper::exitPerl(){
    PERL_SYS_TERM();
}
