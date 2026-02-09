#ifndef FACTORN_SCRIPT_BIGNUM_H
#define FACTORN_SCRIPT_BIGNUM_H

#include <assert.h>
#include <stdint.h>
#include <string>
#include <vector>

#include <gmp.h>

class CScriptBignum {
public:
    explicit CScriptBignum(const int64_t& n)
    {
        mpz_init_set_si(m_value, n);
        m_valid = true;
    }

    explicit CScriptBignum(const mpz_t& n)
    {
        mpz_init_set(m_value, n);
        m_valid = true;
    }

    explicit CScriptBignum(const std::vector<unsigned char>& encoded)
    {
        //import the encoded value
        mpz_init(m_value);
        mpz_import(m_value, encoded.size(), -1, sizeof(unsigned char), 0, 0, encoded.data());

        // check whether a signing bit is set
        if (encoded.size() > 0 && encoded.back() & 0x80) {
            // get the most significant bit
            size_t most_significant_bit = mpz_sizeinbase(m_value, 2) - 1;

            // the most significant bit insize the gmp integer MUST be 1
            if (mpz_tstbit(m_value, most_significant_bit) == 1) {
                // remove the most significant bit from the magnitude
                mpz_clrbit(m_value, most_significant_bit);

                // mark the value as negative
                mpz_neg(m_value, m_value);

                // if not negative 0, this is valid
                m_valid = operator!=(0);
            } else {
                m_valid = false;
            }
        } else {
            m_valid = true;
        }
    }

    explicit CScriptBignum(const std::string decimal)
    {
        int import_result = mpz_init_set_str(m_value, decimal.c_str(), 10);
        m_valid = import_result == 0;
    }

    // copy constructor
    CScriptBignum(const CScriptBignum& n) { CScriptBignum(n.m_value); }

    // destructor takes care of mpz_clear to save headaches everywhere
    ~CScriptBignum() { mpz_clear(m_value); }

    inline bool operator==(const int64_t& rhs) const    { return mpz_cmp_si(m_value, rhs) == 0; }
    inline bool operator!=(const int64_t& rhs) const    { return mpz_cmp_si(m_value, rhs) != 0; }
    inline bool operator<=(const int64_t& rhs) const    { return mpz_cmp_si(m_value, rhs) <= 0; }
    inline bool operator< (const int64_t& rhs) const    { return mpz_cmp_si(m_value, rhs) < 0; }
    inline bool operator>=(const int64_t& rhs) const    { return mpz_cmp_si(m_value, rhs) >= 0; }
    inline bool operator> (const int64_t& rhs) const    { return mpz_cmp_si(m_value, rhs) > 0; }

    inline bool operator==(const CScriptBignum& rhs) const { return mpz_cmp(m_value, rhs.m_value) == 0; }
    inline bool operator!=(const CScriptBignum& rhs) const { return mpz_cmp(m_value, rhs.m_value) != 0; }
    inline bool operator<=(const CScriptBignum& rhs) const { return mpz_cmp(m_value, rhs.m_value) <= 0; }
    inline bool operator< (const CScriptBignum& rhs) const { return mpz_cmp(m_value, rhs.m_value) < 0; }
    inline bool operator>=(const CScriptBignum& rhs) const { return mpz_cmp(m_value, rhs.m_value) >= 0; }
    inline bool operator> (const CScriptBignum& rhs) const { return mpz_cmp(m_value, rhs.m_value) > 0; }

    inline bool sign() const { return operator<(0); }
    inline size_t bits() const { return mpz_sizeinbase(m_value, 2); }

    inline CScriptBignum operator% (const CScriptBignum& rhs) const
    {
        mpz_t mod;
        mpz_init(mod);
        mpz_mod(mod, m_value, rhs.m_value);

        // copy the value into a CScriptBignum first so that we can clear `mod`
        CScriptBignum ret(mod);
        mpz_clear(mod);

        return ret;
    }

    bool IsValid() { return m_valid; }

    std::string GetDec() const {
      size_t decsz = mpz_sizeinbase(m_value, 10);
      if (decsz > 4095) {
          return "unprintable number";
      }
      char dec_cstr[4096];
      gmp_snprintf(dec_cstr, decsz + 1, "%Zd", m_value);
      return dec_cstr;
    }

    std::vector<unsigned char> Serialize() const
    {

        if (operator==(0)) {
            return std::vector<unsigned char>();
        }

        size_t bitsz = bits();

        //NOTE: if we have a byte-aligned number of bits, add an extra byte
        //to contain the signing bit (even if there is nothing to sign)
        size_t bytesz = ((bitsz + 7) / 8) + (bitsz % 8 == 0 ? 1 : 0);

        std::vector<unsigned char> result(bytesz);
        mpz_export(result.data(), &bytesz, -1, 1, 1, NULL, m_value);

        // in all cases, the most significant bit MUST be 0
        assert((result.back() & 0x80) == 0);

        // set the signing bit
        if (sign()) {
            result.back() |= 0x80;
        }

        return result;
    }

private:
    mpz_t m_value;
    bool m_valid;
};


#endif // FACTORN_SCRIPT_BIGNUM_H
