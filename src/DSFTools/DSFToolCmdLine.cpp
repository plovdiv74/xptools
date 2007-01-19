/* 
 * Copyright (c) 2004, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
 * THE SOFTWARE.
 *
 */
#include <stdio.h>
#include "AssertUtils.h"

FILE * err_fi = stdout;

void AssertShellBail(const char * condition, const char * file, int line)
{
	fprintf(err_fi,"ERROR: %s\n", condition);
	fprintf(err_fi,"(%s, %d.)\n", file, line);
	exit(1);
}

bool DSF2Text(const char * inDSF, const char * inFileName);
bool Text2DSF(const char * inFileName, const char * inDSF);
bool ENV2Overlay(const char * inFileName, const char * inDSF);

int main(int argc, char * argv[])
{
	InstallDebugAssertHandler(AssertShellBail);
	InstallAssertHandler(AssertShellBail);

	if (argc < 2) goto help;
	
	for (int n = 1; n < argc; ++n)
	{
		if (!strcmp(argv[n], "-env2overlay"))
		{
			++n;
			if (n >= argc) goto help;
			const char * f1 = argv[n];
			++n;
			if (n >= argc) goto help;
			const char * f2 = argv[n];
			
			printf("Converting %s from ENV to DSF overlay as %s\n", f1, f2);
			if (ENV2Overlay(f1, f2))
				printf("Converted %s to %s\n",f1, f2);
			else
				{ fprintf(err_fi,"ERROR: Error converting %s to %s\n", f1, f2); exit(1); }
		}


		if (!strcmp(argv[n], "-dsf2text"))
		{
			++n;
			if (n >= argc) goto help;
			const char * f1 = argv[n];
			++n;
			if (n >= argc) goto help;
			const char * f2 = argv[n];
			
			if (strcmp(f2,"-")==0)			// If we are directing the DSF text stream to stdout
				err_fi=stderr;				// then put err msgs to stderr.
			
			fprintf(err_fi,"Converting %s from DSF to text as %s\n", f1, f2);
			if (DSF2Text(f1, f2))
				fprintf(err_fi,"Converted %s to %s\n",f1, f2);
			else
				{ fprintf(err_fi,"ERROR: Error convertiong %s to %s\n", f1, f2); exit(1); }
		}
		
		if (!strcmp(argv[n], "-text2dsf"))
		{
			++n;
			if (n >= argc) goto help;
			const char * f1 = argv[n];
			++n;
			if (n >= argc) goto help;
			const char * f2 = argv[n];
			
			printf("Converting %s from text to DSF as %s\n", f1, f2);
			if (Text2DSF(f1, f2))
				printf("Converted %s to %s\n",f1, f2);
			else
				{ fprintf(err_fi, "ERROR: Error convertiong %s to %s\n", f1, f2); exit(1); }
		}		
	}
	
	return 0;
help:
	fprintf(err_fi, "Usage: dsftool -dsf2text [dsffile] [textfile]\n");
	fprintf(err_fi, "		dsftool -text2dsf [textfile] [dsffile]\n");
	fprintf(err_fi, "       dsftool -env2overlay [envfile] [dsffile]\n");
	return 1;
}
