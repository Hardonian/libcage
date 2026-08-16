/*
 * libcage — a zero-dependency pure-C LLM agent runtime for autonomous code repair.
 *
 * Why this exists: every coding agent today is Python/Node. libcage runs where
 * those can't — air-gapped CI, embedded boxes, minimal containers, bare metal
 * with no venv. Single static binary, libc + POSIX sockets only.
 *
 * What it does:
 *   1. Read a prompt + a target source file.
 *   2. Send them to an OpenAI-compatible /chat/completions endpoint.
 *   3. Parse the model's UNIFIED DIFF from its reply.
 *   4. Apply the diff, run a compile command, run a test command.
 *   5. If either fails, feed the error back to the model and loop (max N times).
 *
 * Config (env):
 *   LIBCAGE_API_BASE  default http://localhost:11434/v1   (Ollama-compatible)
 *   LIBCAGE_API_KEY   default "ollama"
 *   LIBCAGE_MODEL     default "qwen2.5-coder:7b"
 *   LIBCAGE_COMPILE   shell cmd to compile (default "cc -o /tmp/libcage_out TARGET")
 *   LIBCAGE_TEST      shell cmd to test (default empty = skip)
 *   LIBCAGE_MAX_ITER  default 5
 *
 * The model is instructed to reply with ONLY a unified diff. We extract the
 * first ```diff ... ``` block (or any @@ ... @@ hunk) and apply it.
 *
 * Build: cc -O2 -std=c11 -o libcage agent.c  (no -l flags needed)
 *
 * This file is intentionally dependency-free. No curl, no jansson.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#ifndef LIBCAGE_VERSION
#define LIBCAGE_VERSION "0.1.0"
#endif

/* ---------- small dynamic string ---------- */
typedef struct { char *p; size_t len, cap; } Str;
static void str_init(Str *s){ s->p=NULL; s->len=0; s->cap=0; }
static int str_append(Str *s, const char *data, size_t n){
    if(s->len+n+1 > s->cap){
        size_t nc = s->cap? s->cap*2 : 256;
        while(nc < s->len+n+1) nc*=2;
        char *np = realloc(s->p, nc);
        if(!np) return -1;
        s->p=np; s->cap=nc;
    }
    memcpy(s->p+s->len, data, n);
    s->len+=n; s->p[s->len]=0;
    return 0;
}
static int str_puts(Str *s, const char *c){ return str_append(s, c, strlen(c)); }

/* ---------- HTTP POST via POSIX sockets (no libcurl) ---------- */
static int http_post(const char *host, const char *port, const char *path,
                     const char *body, const char *auth, Str *resp){
    struct addrinfo hints, *res=NULL;
    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(host, port, &hints, &res)!=0) return -1;
    int fd=-1;
    for(struct addrinfo *r=res; r; r=r->ai_next){
        fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if(fd<0) continue;
        if(connect(fd, r->ai_addr, r->ai_addrlen)==0) break;
        close(fd); fd=-1;
    }
    freeaddrinfo(res);
    if(fd<0) return -1;

    Str req; str_init(&req);
    char clen[32]; snprintf(clen,sizeof clen,"%zu",strlen(body));
    str_puts(&req,"POST "); str_puts(&req,path); str_puts(&req," HTTP/1.1\r\n");
    str_puts(&req,"Host: "); str_puts(&req,host); str_puts(&req,"\r\n");
    str_puts(&req,"Content-Type: application/json\r\n");
    if(auth && *auth){
        str_puts(&req,"Authorization: Bearer "); str_puts(&req,auth); str_puts(&req,"\r\n");
    }
    str_puts(&req,"Content-Length: "); str_puts(&req,clen); str_puts(&req,"\r\n");
    str_puts(&req,"Connection: close\r\n\r\n");
    str_puts(&req,body);

    size_t sent=0;
    while(sent < req.len){
        ssize_t w = write(fd, req.p+sent, req.len-sent);
        if(w<=0){ close(fd); free(req.p); return -1; }
        sent+=w;
    }
    free(req.p);

    char buf[4096];
    Str body_acc; str_init(&body_acc);
    /* read until close */
    while(1){
        ssize_t n = read(fd, buf, sizeof buf);
        if(n<=0) break;
        str_append(&body_acc, buf, (size_t)n);
    }
    close(fd);

    /* strip headers: find \r\n\r\n */
    char *h_end = strstr(body_acc.p, "\r\n\r\n");
    if(!h_end){ free(body_acc.p); return -1; }
    char *b = h_end + 4;
    str_init(resp);
    str_puts(resp, b);
    free(body_acc.p);
    return 0;
}

/* ---------- minimal JSON string escaper ---------- */
static void json_escape(Str *out, const char *s){
    str_puts(out,"\"");
    for(; *s; ++s){
        switch(*s){
            case '"': str_puts(out,"\\\""); break;
            case '\\': str_puts(out,"\\\\"); break;
            case '\n': str_puts(out,"\\n"); break;
            case '\t': str_puts(out,"\\t"); break;
            case '\r': str_puts(out,"\\r"); break;
            default:
                if((unsigned char)*s < 0x20){ char tmp[8]; snprintf(tmp,sizeof tmp,"\\u%04x",*s); str_puts(out,tmp); }
                else str_append(out, s, 1);
        }
    }
    str_puts(out,"\"");
}

/* extract the model's content from a minimal JSON parse (find first "content":"...") */
static char *extract_content(const char *json){
    const char *needle = "\"content\"";
    const char *p = strstr(json, needle);
    if(!p) return NULL;
    p += strlen(needle);
    while(*p && *p!=':') p++;
    if(*p!=':') return NULL;
    p++;
    while(*p && isspace((unsigned char)*p)) p++;
    if(*p!='"') return NULL;
    p++;
    Str s; str_init(&s);
    while(*p && *p!='"'){
        if(*p=='\\' && p[1]=='"'){ str_append(&s,"\"",1); p+=2; continue; }
        if(*p=='\\' && p[1]=='n'){ str_append(&s,"\n",1); p+=2; continue; }
        if(*p=='\\' && p[1]=='t'){ str_append(&s,"\t",1); p+=2; continue; }
        if(*p=='\\' && p[1]=='r'){ str_append(&s,"\r",1); p+=2; continue; }
        if(*p=='\\' && p[1]=='\\'){ str_append(&s,"\\",1); p+=2; continue; }
        if(*p=='\\' && p[1]=='u' && p[2] && p[3] && p[4] && p[5]){
            /* decode \uXXXX -> UTF-8 byte(s). Handles BMP; sufficient for < > & ' " */
            unsigned int cp=0;
            for(int k=0;k<4;k++){ char c=p[2+k];
                cp<<=4; if(c>='0'&&c<='9') cp|=c-'0';
                else if(c>='a'&&c<='f') cp|=c-'a'+10;
                else if(c>='A'&&c<='F') cp|=c-'A'+10; }
            /* encode cp as UTF-8 */
            if(cp<0x80){ char c=(char)cp; str_append(&s,&c,1); }
            else if(cp<0x800){ char b0=0xC0|(cp>>6), b1=0x80|(cp&0x3F); str_append(&s,&b0,1); str_append(&s,&b1,1); }
            else { char b0=0xE0|(cp>>12), b1=0x80|((cp>>6)&0x3F), b2=0x80|(cp&0x3F);
                   str_append(&s,&b0,1); str_append(&s,&b1,1); str_append(&s,&b2,1); }
            p+=6; continue;
        }
        str_append(&s,p,1); p++;
    }
    return s.p? s.p : NULL;
}

/* extract a ```diff ... ``` or first @@ hunk block from model text */
static char *extract_diff(const char *text){
    /* try fenced diff block */
    const char *f = strstr(text, "```diff");
    if(f){
        f += 7;
        const char *end = strstr(f, "```");
        if(end){
            size_t n = (size_t)(end-f);
            char *out = malloc(n+1);
            memcpy(out, f, n); out[n]=0;
            return out;
        }
    }
    /* fallback: find first "@@" */
    const char *a = strstr(text, "@@");
    if(a){
        /* take from @@ to end of a hunks section: stop at next blank-line-after-context or EOF */
        const char *e = a;
        /* include until we hit a line that is not [ +/- @ \\ ] and is empty */
        const char *p = a;
        while(*p){
            if(*p=='\n'){
                const char *q=p+1;
                if(*q && *q!='+' && *q!='-' && *q!='@' && *q!=' ' && *q!='\\') break;
            }
            p++;
        }
        e=p;
        size_t n=(size_t)(e-a);
        char *out=malloc(n+1); memcpy(out,a,n); out[n]=0;
        return out;
    }
    return NULL;
}

/* extract a complete file from a ```c / ```cpp / ```c fenced block.
   Preferred over diff because it is robust to small mismatches. */
static char *extract_file(const char *text){
    static const char *tags[] = {"```c\n", "```cpp\n", "```c", "```cpp", NULL};
    for(int i=0; tags[i]; i++){
        const char *f = strstr(text, tags[i]);
        if(!f) continue;
        f += strlen(tags[i]);
        if(*f=='\n') f++;
        const char *end = strstr(f, "```");
        if(!end) end = f + strlen(f);
        size_t n = (size_t)(end - f);
        char *out = malloc(n+1);
        memcpy(out, f, n); out[n]=0;
        return out;
    }
    return NULL;
}

/* apply a unified diff to file in-place (supports @@ hunks, + - lines) */
static int apply_diff(const char *path, const char *diff){
    FILE *f = fopen(path,"r");
    if(!f) return -1;
    Str original; str_init(&original);
    char line[8192];
    while(fgets(line,sizeof line,f)) str_puts(&original,line);
    fclose(f);

    /* parse hunks: we do a line-by-line patch using context match */
    Str result; str_init(&result);
    const char *p = diff;
    /* skip to first @@ */
    const char *h = strstr(p,"@@");
    if(!h) return -1;
    /* copy header (anythin g before first @@ that isn't diff body) */
    /* We require the diff to correspond to the whole file's relevant region.
       Simple strategy: split original into lines, walk diff hunks, for each hunk
       match context lines then apply adds/removes. */
    /* tokenize original lines */
    /* For robustness we implement a minimal patcher that operates on the full
       original text using @@ offsets is complex; instead we apply a simpler
       approach: rebuild file by scanning original lines and consuming hunks. */

    /* Convert original to array of lines */
    /* (kept simple: store line starts) */
    size_t cap=0, nlines=0;
    char **lines = NULL;
    {
        const char *s=original.p; const char *start=s;
        while(*s){
            if(*s=='\n'){
                size_t l=(size_t)(s-start)+1;
                if(nlines>=cap){cap=cap?cap*2:64; lines=realloc(lines,cap*sizeof(char*));}
                char *cp=malloc(l+1); memcpy(cp,start,l); cp[l]=0;
                lines[nlines++]=cp;
                start=s+1;
            }
            s++;
        }
        if(start!=s){
            size_t l=(size_t)(s-start);
            if(nlines>=cap){cap=cap?cap*2:64; lines=realloc(lines,cap*sizeof(char*));}
            char *cp=malloc(l+1); memcpy(cp,start,l); cp[l]=0; lines[nlines++]=cp;
        }
    }

    size_t oi=0; /* original index */
    /* walk hunks */
    while((h=strstr(p,"@@"))){
        /* parse old start from @@ -old_start,... +new_start,... @@ */
        int old_start=0;
        const char *q=h+2;
        if(*q=='-'){ q++;
            old_start=atoi(q); while(*q && *q!=',') q++;
        }
        /* skip to end of hunk header (second @@) */
        const char *he = strstr(h,"@@");
        if(!he){ break; }
        const char *body = he+2;
        /* find next hunk or end */
        const char *next = strstr(body,"@@");
        const char *bend = next? next : body+strlen(body);

        /* old_start is 1-based; advance oi to old_start-1 */
        long target = old_start-1;
        if(target<0) target=0;
        /* emit original lines up to target (context before hunk) */
        while(oi < (size_t)target && oi < nlines){
            str_puts(&result, lines[oi]); oi++;
        }
        /* now process hunk body lines between body..bend */
        /* We will consume original lines as we hit context/removal, and emit
           additions. We track a separate cursor for original consumption. */
        /* parse hunk body lines */
        const char *bp = body;
        while(bp < bend){
            /* get one hunk line */
            const char *le = bp;
            while(*le && *le!='\n') le++;
            size_t ll=(size_t)(le-bp);
            /* ll excludes newline; include newline if present */
            int has_nl = (*le=='\n');
            char lbuf[8192];
            if(ll>=sizeof lbuf) ll=sizeof lbuf-1;
            memcpy(lbuf,bp,ll); lbuf[ll]=0;

            char op = lbuf[0];
            if(op=='+'){
                str_append(&result, lbuf+1, ll-1);
                if(has_nl) str_append(&result,"\n",1);
            } else if(op=='-'){
                /* remove: advance original cursor past the matching line */
                if(oi<nlines) oi++;
            } else if(op==' ' || op=='\\'){
                /* context: emit original line (or the context line itself) */
                /* Prefer original to preserve exact bytes */
                if(oi<nlines){ str_puts(&result, lines[oi]); oi++; }
                else { str_append(&result, lbuf+1, ll-1); if(has_nl) str_append(&result,"\n",1); }
            } else {
                /* unknown — emit as-is */
                str_append(&result, lbuf, ll);
                if(has_nl) str_append(&result,"\n",1);
            }
            bp = has_nl? le+1 : le;
        }
        p = next? next : bend+strlen(bend);
    }
    /* emit remaining original lines */
    while(oi<nlines){ str_puts(&result, lines[oi]); oi++; }

    /* write back */
    FILE *o=fopen(path,"w");
    if(!o){ /* free */ return -1; }
    fputs(result.p, o);
    fclose(o);

    /* free lines */
    for(size_t i=0;i<nlines;i++) free(lines[i]);
    free(lines);
    free(result.p); free(original.p);
    return 0;
}

/* run a shell command, return 0 if success */
static int run_cmd(const char *cmd){
    int r = system(cmd);
    return r;
}

/* ===================== LIBRAGE PRO (paid tier) =====================
 * Features: SBOM generation, team policy enforcement (endpoint allowlist),
 * and a license gate. All dependency-free, same zero-dep philosophy.
 * Unlocked by `--pro <license_file>` where the file is non-empty.
 * =================================================================== */

/* Minimal SHA-256 (public-domain style, compact). Returns 0 on success. */
static void sha256(const unsigned char *msg, size_t len, unsigned char out[32]){
    static const unsigned K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    unsigned H[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    /* padded message */
    size_t newlen = ((len + 8) / 64 + 1) * 64;
    unsigned char *m = calloc(newlen, 1);
    if(!m) return;
    memcpy(m, msg, len);
    m[len] = 0x80;
    unsigned long long bits = (unsigned long long)len * 8;
    for(int i=0;i<8;i++) m[newlen-1-i] = (unsigned char)(bits >> (i*8));
    for(size_t off=0; off<newlen; off+=64){
        unsigned w[64];
        for(int i=0;i<16;i++) w[i] = (m[off+i*4]<<24)|(m[off+i*4+1]<<16)|(m[off+i*4+2]<<8)|m[off+i*4+3];
        for(int i=16;i<64;i++){
            unsigned s0 = (w[i-15]>>2|w[i-15]<<30)^(w[i-15]>>13|w[i-15]<<19)^(w[i-15]>>22);
            unsigned s1 = (w[i-2]>>6|w[i-2]<<26)^(w[i-2]>>11|w[i-2]<<21)^(w[i-2]>>25);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        unsigned a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
        for(int i=0;i<64;i++){
            unsigned S1=(e>>6|e<<26)^(e>>11|e<<21)^(e>>25); unsigned ch=(e&f)^((~e)&g);
            unsigned t=h+S1+ch+K[i]+w[i]; unsigned S0=(a>>2|a<<30)^(a>>13|a<<19)^(a>>22);
            unsigned maj=(a&b)^(a&c)^(b&c); unsigned t2=S0+maj;
            h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+t2;
        }
        H[0]+=a;H[1]+=b;H[2]+=c;H[3]+=d;H[4]+=e;H[5]+=f;H[6]+=g;H[7]+=h;
    }
    for(int i=0;i<8;i++){ out[i*4]=(H[i]>>24)&0xff; out[i*4+1]=(H[i]>>16)&0xff; out[i*4+2]=(H[i]>>8)&0xff; out[i*4+3]=H[i]&0xff; }
    free(m);
}

/* SBOM: CycloneDX-lite JSON. Reports compiler + source hash + zero deps. */
static int cmd_sbom(const char *target){
    /* compiler version */
    Str cv; str_init(&cv);
    FILE *pp = popen("cc --version 2>/dev/null", "r");
    if(pp){ char b[512]; if(fgets(b,sizeof b,pp)) str_puts(&cv,b); pclose(pp); }
    /* sha256 of target */
    FILE *f = fopen(target,"rb");
    unsigned char hash[32]; memset(hash,0,32);
    if(f){
        fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
        unsigned char *buf = malloc(sz>0?sz:1);
        if(buf){ (void)fread(buf,1,(size_t)sz,f); sha256(buf,(size_t)sz,hash); free(buf); }
        fclose(f);
    }
    char hex[65]; for(int i=0;i<32;i++) snprintf(hex+i*2,3,"%02x",hash[i]);
    printf("{\n");
    printf("  \"bomFormat\": \"CycloneDX\", \"specVersion\": \"1.5\", \"version\": 1,\n");
    printf("  \"metadata\": {\"component\": {\"type\":\"application\",\"name\":\"libcage\",\"version\":\"%s\"}},\n", LIBCAGE_VERSION);
    printf("  \"components\": [\n");
    printf("    {\"type\":\"library\",\"name\":\"libc (host)\",\"version\":%.*s,\"hashes\":[{\"alg\":\"SHA-256\",\"content\":\"%s\"}],\"scope\":\"required\"},\n",
           (int)strcspn(cv.p,"\n"), cv.p, hex);
    printf("    {\"type\":\"file\",\"name\":\"%s\",\"hashes\":[{\"alg\":\"SHA-256\",\"content\":\"%s\"}]}\n", target, hex);
    printf("  ],\n");
    printf("  \"dependencies\": [{\"ref\":\"libcage\",\"dependsOn\":[\"libc (host)\"]}]\n");
    printf("}\n");
    free(cv.p);
    return 0;
}

/* Policy: JSON with "allowed_endpoints": ["https://...", ...]. Refuse if host
   not in list. Minimal JSON parse (only reads that one array). */
static int policy_allows(const char *policyfile, const char *host){
    FILE *f = fopen(policyfile,"r");
    if(!f) return 0;
    Str js; str_init(&js);
    char b[4096]; while(fgets(b,sizeof b,f)) str_puts(&js,b);
    fclose(f);
    /* find "allowed_endpoints" then collect "https://host..." entries */
    const char *p = strstr(js.p, "\"allowed_endpoints\"");
    if(!p){ free(js.p); return 0; }
    int ok=0;
    const char *q = strchr(p,'[');
    if(!q){ free(js.p); return 0; }
    /* scan string literals in the array until matching ']' at depth 0 */
    const char *s = q;
    while(*s && *s!=']'){
        if(*s=='"'){
            s++;
            Str lit; str_init(&lit);
            while(*s && *s!='"'){ str_append(&lit,s,1); s++; }
            if(*s=='"') s++;
            if(strstr(lit.p, host)) ok=1;
            free(lit.p);
        } else s++;
    }
    free(js.p);
    return ok;
}

static int is_pro(const char *licfile){
    if(!licfile) return 0;
    FILE *f = fopen(licfile,"r");
    if(!f) return 0;
    char b[256]; size_t n = fread(b,1,sizeof b-1,f); fclose(f);
    /* non-empty (after trimming) license => pro unlocked */
    for(size_t i=0;i<n;i++) if(!isspace((unsigned char)b[i])) return 1;
    return 0;
}

static void usage(const char *me){
    fprintf(stderr,
        "libcage %s — pure-C LLM agent for autonomous code repair\n"
        "usage: %s <prompt> <target_file> [compile_cmd] [test_cmd]\n"
        "env: LIBCAGE_API_BASE (default http://localhost:11434/v1)\n"
        "     LIBCAGE_API_KEY (default ollama)  LIBCAGE_MODEL (default qwen2.5-coder:7b)\n"
        "     LIBCAGE_MAX_ITER (default 5)\n"
        "PRO (--pro <license_file>): --sbom (CycloneDX SBOM)  --policy <file.json> (endpoint allowlist)\n",
        LIBCAGE_VERSION, me);
}

int main(int argc, char **argv){
    if(argc<3){ usage(argv[0]); return 2; }

    /* Pro flags: --pro LICENSE, --sbom, --policy FILE. Parse before positional. */
    const char *pro_lic = NULL;
    const char *policy_file = NULL;
    int want_sbom = 0;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--pro")==0 && i+1<argc){ pro_lic=argv[++i]; }
        else if(strcmp(argv[i],"--sbom")==0){ want_sbom=1; }
        else if(strcmp(argv[i],"--policy")==0 && i+1<argc){ policy_file=argv[++i]; }
    }
    if(want_sbom){
        if(!is_pro(pro_lic)){ fprintf(stderr,"libcage: --sbom requires --pro <license_file>\n"); return 2; }
        /* target = first positional arg (not a flag or flag-value) */
        const char *sbom_target = NULL;
        for(int i=1;i<argc;i++){
            if(strcmp(argv[i],"--pro")==0){ i++; continue; }
            if(strcmp(argv[i],"--sbom")==0) continue;
            if(strcmp(argv[i],"--policy")==0){ i++; continue; }
            sbom_target = argv[i]; break;
        }
        if(!sbom_target){ fprintf(stderr,"libcage: --sbom requires a target file\n"); return 2; }
        return cmd_sbom(sbom_target);
    }
    if(policy_file && !is_pro(pro_lic)){
        fprintf(stderr,"libcage: --policy requires --pro <license_file>\n"); return 2;
    }

    const char *prompt = argv[1];
    const char *target = argv[2];
    const char *compile = argc>3? argv[3] : "cc -o /tmp/libcage_out TARGET";
    const char *testcmd = argc>4? argv[4] : "";

    /* read target file */
    FILE *tf=fopen(target,"r");
    if(!tf){ fprintf(stderr,"libcage: cannot open target %s: %s\n",target,strerror(errno)); return 1; }
    Str src; str_init(&src);
    char buf[65536];
    while(fgets(buf,sizeof buf,tf)) str_puts(&src,buf);
    fclose(tf);

    char *api_base = getenv("LIBCAGE_API_BASE") ?: "http://localhost:11434/v1";
    char *api_key  = getenv("LIBCAGE_API_KEY")  ?: "ollama";
    char *model    = getenv("LIBCAGE_MODEL")    ?: "qwen2.5-coder:7b";
    int max_iter   = atoi(getenv("LIBCAGE_MAX_ITER") ?: "5");
    if(max_iter<1) max_iter=1;

    /* parse host/port/path from api_base (assume http://host:port/path) */
    char host[256]={0}, port[16]={0}, path[512]={0};
    {
        const char *u = api_base;
        if(strncmp(u,"http://",7)==0) u+=7;
        const char *slash = strchr(u,'/');
        const char *colon = strchr(u,':');
        if(colon && (!slash || colon<slash)){
            size_t hl=(size_t)(colon-u); if(hl>=sizeof host) hl=sizeof host-1;
            memcpy(host,u,hl); host[hl]=0;
            const char *pp=colon+1;
            const char *ps = slash? slash : pp+strlen(pp);
            size_t pl=(size_t)(ps-pp); if(pl>=sizeof port) pl=sizeof port-1;
            memcpy(port,pp,pl); port[pl]=0;
        } else {
            size_t hl = slash? (size_t)(slash-u) : strlen(u);
            if(hl>=sizeof host) hl=sizeof host-1;
            memcpy(host,u,hl); host[hl]=0;
            strcpy(port,"80");
        }
        if(slash) snprintf(path,sizeof path,"%s",slash);
        else snprintf(path,sizeof path,"/");
        if(strcmp(port,"")==0) strcpy(port,"80");
    }

    int iter=0;
    char *last_error = "";
    while(iter < max_iter){
        iter++;
        fprintf(stderr,"libcage: iteration %d/%d\n", iter, max_iter);

        /* build messages JSON */
        Str sysmsg; str_init(&sysmsg);
        str_puts(&sysmsg,
            "You are a code repair agent. Reply with the COMPLETE corrected file contents "
            "inside a single ```c fenced code block (no explanation, no diff, just the full file). "
            "The file you return must compile and satisfy the task.");
        Str usrmsg; str_init(&usrmsg);
        str_puts(&usrmsg, "FILE: "); str_puts(&usrmsg, target); str_puts(&usrmsg, "\n\n");
        str_puts(&usrmsg, "CURRENT CONTENTS:\n```\n"); str_puts(&usrmsg, src.p); str_puts(&usrmsg, "\n```\n\n");
        str_puts(&usrmsg, "TASK: "); str_puts(&usrmsg, prompt); str_puts(&usrmsg, "\n\n");
        if(last_error && *last_error){
            str_puts(&usrmsg, "PREVIOUS ATTEMPT FAILED WITH:\n"); str_puts(&usrmsg, last_error); str_puts(&usrmsg, "\n");
        }

        Str body; str_init(&body);
        str_puts(&body, "{\"model\":");
        json_escape(&body, model);
        str_puts(&body, ",\"messages\":[{\"role\":\"system\",\"content\":");
        json_escape(&body, sysmsg.p);
        str_puts(&body, "},{\"role\":\"user\",\"content\":");
        json_escape(&body, usrmsg.p);
        str_puts(&body, "}],\"temperature\":0.1}");
        free(sysmsg.p); free(usrmsg.p);

        Str resp; str_init(&resp);
        char urlpath[600]; snprintf(urlpath,sizeof urlpath,"%s/chat/completions", path);
        /* Pro policy enforcement: refuse endpoints not in the allowlist */
        if(policy_file && !policy_allows(policy_file, host)){
            fprintf(stderr,"libcage: endpoint %s not allowed by policy %s\n", host, policy_file);
            free(body.p); free(resp.p);
            return 1;
        }

        if(http_post(host, port, urlpath, body.p, api_key, &resp)!=0){
            fprintf(stderr,"libcage: HTTP request failed\n");
            free(body.p); free(resp.p);
            return 1;
        }
        free(body.p);

        char *content = extract_content(resp.p);
        if(!content){
            fprintf(stderr,"libcage: no content in response\n");
            free(resp.p);
            return 1;
        }

        char *newsrc = extract_file(content);   /* preferred: full file */
        int applied = 0;
        if(newsrc){
            FILE *o = fopen(target,"w");
            if(o){ fputs(newsrc, o); fclose(o); applied = 1; }
            free(newsrc);
        }
        if(!applied){
            /* fallback: unified diff */
            char *diff = extract_diff(content);
            if(!diff){
                fprintf(stderr,"libcage: no file/diff found in model response\n");
                free(content); free(resp.p);
                return 1;
            }
            if(apply_diff(target, diff)!=0){
                fprintf(stderr,"libcage: failed to apply diff\n");
                free(diff); free(content); free(resp.p);
                return 1;
            }
            free(diff);
        }
        free(content);
        free(resp.p);

        /* re-read source for next iteration context */
        FILE *rf=fopen(target,"r");
        if(rf){ str_init(&src); while(fgets(buf,sizeof buf,rf)) str_puts(&src,buf); fclose(rf); }

        /* compile */
        char compile_cmd[4096];
        snprintf(compile_cmd,sizeof compile_cmd,"%s",compile);
        char *t = strstr(compile_cmd,"TARGET");
        if(t){ *t=0; strcat(compile_cmd,target); }
        fprintf(stderr,"libcage: compiling...\n");
        int cr = run_cmd(compile_cmd);
        if(cr!=0){
            last_error = "COMPILE FAILED";
            /* capture compiler stderr? we ran via system; re-run capturing */
            /* For simplicity, set a generic message; real impl would capture. */
            fprintf(stderr,"libcage: compile failed, looping\n");
            continue;
        }

        /* test */
        if(testcmd && *testcmd){
            fprintf(stderr,"libcage: testing...\n");
            int tr = run_cmd(testcmd);
            if(tr!=0){
                last_error = "TEST FAILED";
                fprintf(stderr,"libcage: test failed, looping\n");
                continue;
            }
        }

        fprintf(stderr,"libcage: SUCCESS after %d iteration(s)\n", iter);
        return 0;
    }

    fprintf(stderr,"libcage: gave up after %d iterations\n", max_iter);
    return 1;
}
