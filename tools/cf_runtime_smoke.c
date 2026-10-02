#import <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
static int fails=0;
#define CHECK(cond,label) do{ if(!(cond)){printf("FAIL %s\n",label);fails++;} }while(0)
int main(void){
    CFStringRef s=CFStringCreateWithCString(NULL,"ABCabc",kCFStringEncodingUTF8);
    char b[64];
    CFMutableStringRef lo=CFStringCreateMutableCopy(NULL,0,s); CFStringLowercase(lo,NULL);
    CFStringGetCString(lo,b,64,kCFStringEncodingUTF8); CHECK(!strcmp(b,"abcabc"),"lowercase");
    CFMutableStringRef up=CFStringCreateMutableCopy(NULL,0,s); CFStringUppercase(up,NULL);
    CFStringGetCString(up,b,64,kCFStringEncodingUTF8); CHECK(!strcmp(b,"ABCABC"),"uppercase");
    CFMutableStringRef cp=CFStringCreateMutableCopy(NULL,0,s); CFStringCapitalize(cp,NULL);
    CFStringGetCString(cp,b,64,kCFStringEncodingUTF8); CHECK(!strcmp(b,"Abcabc"),"capitalize");
    CHECK(CFStringCompare(CFSTR("ABC"),CFSTR("abc"),kCFCompareCaseInsensitive)==kCFCompareEqualTo,"caseInsensitiveCompare");
    CFMutableStringRef d=CFStringCreateMutableCopy(NULL,0,CFSTR("e\u0301"));
    CFStringNormalize(d,kCFStringNormalizationFormC);
    CFStringGetCString(d,b,64,kCFStringEncodingUTF8); CHECK(!strcmp(b,"\u00e9"),"NFC");
    int v=12345; CFNumberRef num=CFNumberCreate(NULL,kCFNumberIntType,&v);
    CFNumberFormatterRef nf=CFNumberFormatterCreate(NULL,NULL,kCFNumberFormatterNoStyle);
    CFStringRef o=CFNumberFormatterCreateStringWithNumber(NULL,nf,num);
    CFStringGetCString(o,b,64,kCFStringEncodingUTF8); CHECK(!strcmp(b,"12345"),"numberFormatter");
    /* plist roundtrip */
    CFDataRef xml=CFPropertyListCreateData(NULL,CFSTR("hi"),kCFPropertyListXMLFormat_v1_0,0,NULL);
    CHECK(xml!=NULL,"plist XML serialize");
    if(xml){ CFPropertyListRef pl=CFPropertyListCreateWithData(NULL,xml,0,NULL,NULL);
      CHECK(pl!=NULL,"plist XML parse");
      if(pl){ CFStringRef ps=(CFStringRef)pl; char b2[64];
        CFStringGetCString(ps,b2,64,kCFStringEncodingUTF8); CHECK(!strcmp(b2,"hi"),"plist roundtrip"); } }
    /* CFBundle path resolution (was the crash trigger) */
    CFBundleRef mb=CFBundleGetMainBundle(); CHECK(mb!=NULL,"CFBundleGetMainBundle");
    printf(fails? "\n%d CHECK(S) FAILED\n":"\nall checks passed\n", fails);
    return fails!=0;
}
