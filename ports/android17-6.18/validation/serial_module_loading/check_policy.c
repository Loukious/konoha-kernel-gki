#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sepol/policydb/services.h>

int main(int argc, char **argv)
{
    assert(argc >= 6);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long len = ftell(f);
    assert(len > 0 && !fseek(f, 0, SEEK_SET));
    void *data = malloc(len);
    assert(data && fread(data, 1, len, f) == (size_t)len);
    fclose(f);
    policydb_t policy;
    struct policy_file pf;
    assert(!policydb_init(&policy));
    policy_file_init(&pf);
    pf.type = PF_USE_MEMORY;
    pf.data = data;
    pf.len = len;
    assert(!policydb_read(&policy, &pf, 0));
    sidtab_t sidtab;
    assert(!sepol_sidtab_init(&sidtab));
    sepol_set_policydb(&policy);
    sepol_set_sidtab(&sidtab);
    sepol_security_id_t src, dst;
    sepol_security_class_t cls;
    assert(!sepol_context_to_sid(argv[2], strlen(argv[2]) + 1, &src));
    assert(!sepol_context_to_sid(argv[3], strlen(argv[3]) + 1, &dst));
    assert(!sepol_string_to_security_class(argv[4], &cls));
    sepol_access_vector_t need = 0, bit;
    for (int i = 5; i < argc; ++i) {
        assert(!sepol_string_to_av_perm(cls, argv[i], &bit));
        need |= bit;
    }
    struct sepol_av_decision decision;
    unsigned reason;
    assert(!sepol_compute_av_reason(src, dst, cls, need, &decision, &reason));
    printf("%s -> %s:%s requested=%x allowed=%x reason=%x\n",
           argv[2], argv[3], argv[4], need, decision.allowed, reason);
    return (need & decision.allowed) == need ? 0 : 1;
}
