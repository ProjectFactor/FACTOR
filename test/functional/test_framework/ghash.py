import hashlib as phash
import decimal
from .crypto_extended import Whirlpool as whirl
from math import floor
from sympy import nextprime

#Set precision to take square roots
decimal.getcontext().prec = 2048

def ghash_from_cblockheader(header, rounds: int) -> int:
    hashPrevBlock = header.hashPrevBlock.to_bytes(32, byteorder='little')
    hashMerkleRoot = header.hashMerkleRoot.to_bytes(32, byteorder='little')
    nNonce = header.nNonce.to_bytes(8, byteorder='little')
    nTime = header.nTime.to_bytes(4, byteorder='little')
    nVersion = header.nVersion.to_bytes(4, byteorder='little')
    nBits = header.nBits.to_bytes(2, byteorder='little')

    passwd = hashPrevBlock + hashMerkleRoot + nNonce
    salt = nVersion + nBits + nTime

    return ghash_internal(rounds, passwd, salt, header.nBits)

def ghash_internal(rounds, passwd, salt, nBits: int) -> int:

    # ////////////////////////////////////////////////////////////////////////////////
    # //                                Scrypt parameters                           //
    # ////////////////////////////////////////////////////////////////////////////////
    # //                                                                            //
    # //  N                  = Iterations count (Affects memory and CPU Usage).     //
    # //  r                  = block size ( affects memory and CPU usage).          //
    # //  p                  = Parallelism factor. (Number of threads).             //
    # //  pass               = Input password.                                      //
    # //  salt               = securely-generated random bytes.                     //
    # //  derived-key-length = how many bytes to generate as output. Defaults to 32.//
    # //                                                                            //
    # // For reference, Litecoin has N=1024, r=1, p=1.                              //
    # ////////////////////////////////////////////////////////////////////////////////
    N = 1 << 12
    r = 1 << 1
    p = 1
    derived = 0

    #Scrypt Hash to 2048-bits hash.
    #Docs:  hashlib.scrypt(password, *, salt, n, r, p, maxmem=0, dklen=64)
    derived = phash.scrypt( passwd, salt=salt, n=N, r=r, p=p, dklen=256)

    #Prepare GMP objects
    prime_mpz, starting_number_mpz, a_mpz, a_inverse_mpz = 0,0,0,0

    for round in range(rounds):
        # ///////////////////////////////////////////////////////////////
        # //      Memory Expensive Scrypt: 1MB required.               //
        # ///////////////////////////////////////////////////////////////
        derived = phash.scrypt( derived, salt=salt,n=N, r=r, p=p, dklen=256 )

        # ///////////////////////////////////////////////////////////////
        # //   Add different types of hashes to the core.              //
        # ///////////////////////////////////////////////////////////////
        # //Count the bits in previous hash.
        pcnt_half1 = int(derived[:128].hex(), 16).bit_count()
        pcnt_half2 = int(derived[128:].hex(), 16).bit_count()

        derived_first_half = 0
        derived_second_half = 0

        # //Hash the first 1024-bits of the 2048-bits hash.
        if (pcnt_half1 % 2 == 0):
            derived_first_half = phash.blake2b( derived[:128] ).digest()
        else:
            derived_first_half = phash.sha3_512( derived[:128] ).digest()

        #Hash the second 1024-bits of the 2048-bits hash.
        if (pcnt_half2 % 2 == 0):
            derived_second_half = phash.blake2b( derived[128:] ).digest()
        else:
            derived_second_half = phash.sha3_512( derived[128:] ).digest()

        derived = derived_first_half + derived[64:128] + derived_second_half + derived[192:]

        # //////////////////////////////////////////////////////////////
        # // Perform expensive math opertions plus simple hashing     //
        # //////////////////////////////////////////////////////////////
        # //Use the current hash to compute grunt work.
        starting_number_mpz = int.from_bytes(bytes.fromhex( derived.hex() ), byteorder="little")
        a_mpz               = int( floor( decimal.Decimal( starting_number_mpz ).sqrt())) # - \ a = floor( M^(1/2) )
        prime_mpz           = int( floor( decimal.Decimal( a_mpz               ).sqrt())) # - \ p = floor( a^(1/2) )
        prime_mpz           = nextprime( prime_mpz )

        # //Compute a^(-1) Mod p
        a_inverse_mpz = pow( a_mpz, -1, prime_mpz )
        a_bytes = a_inverse_mpz.to_bytes((a_inverse_mpz.bit_length() + 7) // 8, 'little')

        # //Xor into current hash digest.
        temp = bytes( int(derived[idx]) ^ int(a_bytes[idx]) for idx in range(len(a_bytes)) )
        derived = temp + derived[len(a_bytes):]

        # //Compute the population count of a_inverse
        irounds = a_inverse_mpz.bit_count() & 0x7f

        # //Branch away
        for jj in range(irounds):
            br = ( int.from_bytes( derived, byteorder="little"  ) & 0xffffffffffffffff ).bit_count()

            # //Power mod
            a_inverse_mpz = pow( a_inverse_mpz, irounds, prime_mpz)

            # //Xor data
            a_bytes = a_inverse_mpz.to_bytes((a_inverse_mpz.bit_length() + 7) // 8, 'little')
            temp = bytes( int(derived[idx]) ^ int(a_bytes[idx]) for idx in range(len(a_bytes)) )
            derived = temp + derived[len(a_bytes):]

            #Hold data temporarily
            derived_temp = 0

            if (br % 3 == 0):
                derived_temp = phash.sha3_512( derived[:128] ).digest()
                derived = derived_temp + derived[64:]
            elif (br % 3 == 2):
                derived_temp = phash.blake2b( derived[128:] ).digest()
                derived = derived[:192] + derived_temp
            else:
                derived_temp = bytes.fromhex( whirl( "".join( [ chr(a) for a in derived ])  ).hexdigest() )
                derived = derived[:112] + derived_temp + derived[176:]

    w = 0
    w = int.from_bytes( derived, byteorder="little"  )| (1 << (int(nBits - 1) ))
    w = w & ( (1 << int(nBits)) - 1 )

    return w
