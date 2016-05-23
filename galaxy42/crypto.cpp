#include "crypto.hpp"
#include "crypto-sodium/ecdh_ChaCha20_Poly1305.hpp"

#include "ntru/include/ntru_crypto.h"
#include <SIDH_internal.h>

#include "trivialserialize.hpp"

#include "filestorage.hpp"

#include "glue_lockedstring_trivialserialize.hpp"

using sodiumpp::locked_string;

/**
 * @TODO
 * Things that need memlocked proving:
 * serialize_bin()
 * load_from_bin()
 * save() and load() - when calling e.g. serialize_bin() / load_from_bin(),
 * ... and them again, when doing file operations (fstream buffers - instead use simply 1 file function to read/write
 entire buffer at once (btw then size file size limit is it in reasonable values when loading etc)
 */


namespace antinet_crypto {

// ==================================================================
// extra implementation tools

bool safe_string_cmp(const std::string & a, const std::string & b) {
	if (a.size() != b.size()) return false;
	return 0 == sodium_memcmp( a.c_str() , b.c_str() , a.size() );
}

sodiumpp::locked_string substr(const sodiumpp::locked_string & str , size_t len) {
	if (len<1) throw std::runtime_error( string("Invalid substring of len=") + to_string(len) );
	sodiumpp::locked_string ret( len );
	assert(ret.size() == len);
	assert(len < str.size());
	for (size_t p=0; p<str.size() && p < ret.size(); ++p) ret.at(p) = str.at(p);
	return ret;
}



// ==================================================================
// debug

std::string to_debug_locked(const sodiumpp::locked_string & data) {
	#if OPTION_DEBUG_SHOW_SECRET_STRINGS
		return to_debug(data.get_string());
	#else
		UNUSED(data);
	#endif
	return "[hidden-secret](locked_string)";
}


std::string to_debug_locked_maybe(const sodiumpp::locked_string & data) {
	return to_debug_locked(data);
}
std::string to_debug_locked_maybe(const std::string & data) {
	return to_debug(data);
}

// ==================================================================


// ==================================================================
namespace string_binary_op {


std::string operator^(const std::string & str1, const std::string & str2) {
	return binary_string_xor(str1,str2);
}

sodiumpp::locked_string operator^(const sodiumpp::locked_string & str1, const sodiumpp::locked_string & str2) {
	return binary_string_xor(str1,str2);
}

sodiumpp::locked_string operator^(const sodiumpp::locked_string & str1, const std::string & str2_un) {
	sodiumpp::locked_string str2(str2_un);
	return binary_string_xor(str1,str2);
}


} // namespace
// ==================================================================



// ==================================================================

// === The crypto types ===

t_crypto_system_type c_crypto_system::get_system_type() const { return e_crypto_system_type_invalid; }

t_crypto_system_type c_symhash_state::get_system_type() const { return e_crypto_system_type_symhash_todo; }

t_crypto_system_type c_multikeys_pub::get_system_type() const { return e_crypto_system_type_multikey_pub; }

t_crypto_system_type c_multikeys_PRV::get_system_type() const { return e_crypto_system_type_multikey_private; }

template <typename TKey>
size_t c_multikeys_general<TKey>::get_count_keys_in_system(t_crypto_system_type crypto_type) const {
	return m_cryptolists_general.at(crypto_type).size();
}

template <typename TKey>
size_t c_multikeys_general<TKey>::get_count_of_systems() const {
	return m_cryptolists_general.size();
}

t_crypto_system_type c_multikeys_PAIR::get_system_type() const { return e_crypto_system_type_multikey_private; }


// ==================================================================
// Random

// TODO move to top of file - group of free functions
uint8_t get_entropy(ENTROPY_CMD cmd, uint8_t *out) {
    static std::ifstream rand_source;
    static locked_string random_byte(1);

    if (cmd == INIT) {
        /* Any initialization for a real entropy source goes here. */
        rand_source.open("/dev/urandom");
        return 1;
    }

    if (out == nullptr)
        return 0;

    if (cmd == GET_NUM_BYTES_PER_BYTE_OF_ENTROPY) {
        /* Here we return the number of bytes needed from the entropy
         * source to obtain 8 bits of entropy.  Maximum is 8.
         */
        *out = 1;
        return 1;
    }

    if (cmd == GET_BYTE_OF_ENTROPY) {
        if (!rand_source.is_open())
            return 0;

        rand_source.get(random_byte[0]);
        *out = static_cast<uint8_t>(random_byte[0]);
        return 1;
    }
    return 0;
}


CRYPTO_STATUS random_bytes_sidh(unsigned int nbytes, unsigned char *random_array) {
	static std::ifstream rand_source("/dev/urandom");
	if (nbytes == 0) {
		return CRYPTO_ERROR;
	}

	for (unsigned int i = 0; i < nbytes; i++) {
		*(random_array + i) = static_cast<unsigned char>(rand_source.get()); // nbytes of random values
	}

	return CRYPTO_SUCCESS;
}

// ==================================================================

t_hash Hash1( const t_hash & hash ) {
    // TODO I know this look horrible, we should implement some (unsigned char <-> char) wrapper
    size_t u_hashmsg_len = hash.length();
    const unsigned char* u_hashmsg;
    u_hashmsg = reinterpret_cast<const unsigned char *>(&hash[0]);

   	const size_t out_u_hash_len = 64; // Hash1_size();
   	assert( out_u_hash_len == Hash1_size() );  // <-- ^-- TODO(rob) constexpr instead?

    assert( out_u_hash_len <=  crypto_generichash_BYTES_MAX );
    unsigned char out_u_hash[out_u_hash_len];

    crypto_generichash(out_u_hash, out_u_hash_len,
                       u_hashmsg, u_hashmsg_len,
                       nullptr, 0);

    return string(reinterpret_cast<char *>(out_u_hash),  out_u_hash_len);
}

t_hash_PRV Hash1_PRV( const t_hash_PRV & hash ) {
    size_t u_hashmsg_len = hash.size();
    const unsigned char* u_hashmsg;
    u_hashmsg = reinterpret_cast<const unsigned char *>( hash.c_str() ); // read locked

   	const size_t out_u_hash_len = 64; // Hash1_size();
   	assert( out_u_hash_len == Hash1_size() );  // <-- ^-- TODO(rob) constexpr instead?

    assert( out_u_hash_len <=  crypto_generichash_BYTES_MAX );
		t_hash_PRV out_u_hash(out_u_hash_len);

    crypto_generichash(
			reinterpret_cast<unsigned char*>( out_u_hash.buffer_writable() ), // write locked
			out_u_hash_len,
      u_hashmsg, u_hashmsg_len, nullptr, 0);

    return out_u_hash;
}


size_t Hash1_size() {
	return 64;
}

t_hash Hash2( const t_hash & hash ) {
    t_hash hash_from_hash = Hash1(hash);
    for(auto &ch : hash_from_hash) { // negate all octets in it
        ch = ~ch;
    }
    const auto ret = Hash1(hash_from_hash);
    assert( ret.size() == Hash2_size() );
    return ret;
}

t_hash_PRV Hash2_PRV( const t_hash_PRV & hash ) {
    t_hash_PRV hash_from_hash = Hash1_PRV(hash);
    //for(auto &ch : hash_from_hash) { // negate all octets in it
		for (size_t p=0; p<hash_from_hash.size(); ++p) { // TODO use foreach when locked_string supports it
			char & ch = hash_from_hash.at(0); // TODO performance, change to [] after review
    	ch = ~ch;
    }
    const auto ret = Hash1_PRV(hash_from_hash);
    assert( ret.size() == Hash2_size() );
    return ret;
}

size_t Hash2_size() {
	return 64;
}

// ==================================================================

std::string t_crypto_system_type_to_name(int val) {
	switch(val) {
		case e_crypto_system_type_X25519:			return "X25519";
		case e_crypto_system_type_Ed25519:			return "Ed25519";
		case e_crypto_system_type_NTRU_EES439EP1:     return "NTRU-EES439EP1";
		case e_crypto_system_type_SIDH:			return "SIDH-p751";
		case e_crypto_system_type_geport_todo:			return "geport_todo";
		case e_crypto_system_type_symhash_todo:			return "symhash_todo";
		case e_crypto_system_type_multikey_pub:			return "multikey-pub";
		case e_crypto_system_type_multikey_private:			return "multikey-PRIVATE";
			//default:		return "Wrong type";
	}
	return string("(Invalid enum type=") + to_string(val) + string(")");
}

std::string enum_name(t_crypto_system_type e) {
	return t_crypto_system_type_to_name( e );
}

char t_crypto_system_type_to_ID(int val) {
	switch(val) {
		case e_crypto_system_type_X25519: return 'x';
		case e_crypto_system_type_Ed25519: return 'e';
		case e_crypto_system_type_NTRU_EES439EP1: return 't';
		case e_crypto_system_type_SIDH: return 's';
		case e_crypto_system_type_geport_todo: return 'g';
	}
	throw std::invalid_argument("[" + std::string(__func__) + "] Unknown crypto type (val == " + std::to_string(val) + ")");
}

t_crypto_system_type t_crypto_system_type_from_ID(char name) {
	switch(name) {
		case 'x': return e_crypto_system_type_X25519;
		case 'e': return e_crypto_system_type_Ed25519;
		case 't': return e_crypto_system_type_NTRU_EES439EP1;
		case 's': return e_crypto_system_type_SIDH;
		case 'g': return e_crypto_system_type_geport_todo;
	}
	throw std::invalid_argument("[" + std::string(__func__) + "] Unknown crypto type (name == " + name + ")");
}

bool t_crypto_system_type_is_asymkex(t_crypto_system_type sys) {
	switch (sys) {
		case e_crypto_system_type_X25519: return false;
		case e_crypto_system_type_SIDH: return false;
		case e_crypto_system_type_NTRU_EES439EP1: return true;
		default: break;	// other things are not a KEX at all:
	}
	throw std::invalid_argument("[" + std::string(__func__) + "] Unknown crypto type (name == " 
		+ t_crypto_system_type_to_name(sys) + ")");
}


// ==================================================================
// c_symhash_state

c_symhash_state::c_symhash_state( t_hash initial_state )
	: m_state( initial_state )
{
//	_info("Initial state: " << m_state);
//	_info("Initial state dbg: " << string_as_dbg(m_state).get() );
}

void c_symhash_state::next_state( t_hash additional_secret_material ) {
	m_state = Hash1( Hash1( m_state ) + additional_secret_material );
	//_info("State:" << m_state.bytes);
	++m_number;
}

t_hash c_symhash_state::get_password() const {
	return Hash2( m_state );
}


t_hash c_symhash_state::get_the_SECRET_PRIVATE_state() const {
	return m_state;
}


// ##################################################################
// ##################################################################
// all about the multikeys

// ==================================================================
// c_multikeys_general<>

template <typename TKey>
c_multikeys_general<TKey>::c_multikeys_general(t_key_type_secop secop)
 : m_type_secop( secop )
{ }

template <typename TKey>
bool c_multikeys_general<TKey>::operator>(const t_self &rhs) const {
	return this->get_hash() > rhs.get_hash();
}

template <typename TKey>
std::string c_multikeys_general<TKey>::get_hash() const {
	if (m_hash_cached=="") update_hash();
	assert(m_hash_cached != "");
	string s;
	string b=to_debug(s);
	_info("Hash of this multikey:"  << to_debug(m_hash_cached));
	return m_hash_cached;
}

template <typename TKey>
void c_multikeys_general<TKey>::update_hash() const {
	string all_pub; // all public keys together
	m_hash_cached = Hash1( this->serialize_bin()  );
}

template <typename TKey>
void c_multikeys_general<TKey>::add_key(t_crypto_system_type type, const t_key & pubkey) {
	auto & sys_vector = m_cryptolists_general.at( type );
	//_note("ADD KEY: size before: " << sys_vector.size());
	sys_vector.push_back( pubkey );
	//_note("ADD KEY: size after: " << sys_vector.size());
	//_note("ADD KEY: RESULT IS: " << to_debug(serialize_bin()));
	//_note("ADD KEY: THE newest element IS: " << to_debug_locked_maybe( sys_vector.at( sys_vector.size()-1 )  ));
}

template <typename TKey>
typename c_multikeys_general<TKey>::t_key  c_multikeys_general<TKey>::get_key(
t_crypto_system_type crypto_type, size_t number_of_key) const
{
	// TODO check range
	return m_cryptolists_general.at(crypto_type).at(number_of_key);
}



/**

Serialiation format

TODO(hb) this is comment that should be linked from the serialize_bin() function

	A sparse "map" of keys type => of vectors of strings

	format: there are 2 types of keys: (
		keys type 5:  ( 3 keys of this type: varstr varstr varstr )
		keys type 42: ( 2 keys of this type: varstr varstr ) )

	format: 2 ( 5 ( 3 varstr varstr varstr )  42 ( 2 varstr varstr ) )

Example (as of commit after d3b9872f758a90541dde8fc2cf45a97b691e1a17)
Serialized pubkeys: [(104)[
0x02(02) - two key types
0x15(21) - first type is 21 (enum numbers changed for the test)
0x02(02) - two pieces of this key:
, , - it is a 32 char string
0x9f(159),0x9e(158),0x09(09),,+,V,0xc6(198),g,*,b,0xf3(243),w,a,,0x9d(157),0xf1(241),0xd6(214),0xdd(221),0x83(131),X,0xd0(208),0x97(151),0xbd(189),0xef(239),0xc6(198),z,U,0x97(151),0xd4(212),0x10(16),u,;
, ,0x11(17),3,\,0xe0(224),0xdc(220),0xc7(199),H,0xed(237),0x81(129),X,0xd1(209),0xca(202),=,A,0x15(21),0xd3(211),V,0xc0(192),&,0x90(144),",X,0xe4(228),0xaa(170),0x92(146),0xa2(162),0x85(133),0xd6(214),2,0xb7(183),0xcd(205),0x04(04),

0x17(23) - now key type 23,
0x01(01) - 1 piece of it
, ,f,0x80(128),0x81(129),$,0x98(152),0x89(137),0x0f(15),0xde(222),0xbc(188),0xbc(188),0x1f(31),g,0xc9(201),R,6,0x94(148),j,0xd2(210),0x8f(143),0xd5(213),0x98(152),q,0xb7(183),3,[,0xb2(178),0xb4(180),0x8c(140),0xa5(165),0xdf(223),.,b]]
	*/

template <typename TKey>
std::string c_multikeys_general<TKey>::serialize_bin() const { ///< returns a string with all our data serialized, to a binary format
	trivialserialize::generator gen(100);
	gen.push_bytes_n(3,"GMK"); // magic marker - GMK - "Galaxy MultiKey"
	gen.push_byte_u( (char) 'a' ); // version of this map. '$' will be development, and then use 'a','b',... for stable formats
	gen.push_byte_u( m_type_secop ); // marker is it open or secret
	int used_types=0; // count how many key types are actually used - we will count below
	for (size_t ix=0; ix<m_cryptolists_general.size(); ++ix) if (m_cryptolists_general.at(ix).size()) ++used_types;
	gen.push_integer_uvarint(used_types); // save the size of crypto list (number of main elements)
	int used_types_check=0; // counter just to assert
	for (size_t ix=0; ix<m_cryptolists_general.size(); ++ix) { // for all key type (for each element)
		const vector<TKey> & pubkeys_of_this_system  = m_cryptolists_general.at(ix); // take vector of keys
		if (pubkeys_of_this_system.size()) { // save them this time
			++used_types_check;
			gen.push_integer_uvarint(  t_crypto_system_type_to_ID(ix) ); // save key type
			gen.push_vector_object( pubkeys_of_this_system ); // save the vector of keys
		}
	}
	assert(used_types_check == used_types); // we written same amount of keys as we previously counted
	return gen.str();
}

template <typename TKey>
void c_multikeys_general<TKey>::load_from_bin(const std::string & data) {
	// clear(); // remove all keys TODO

	trivialserialize::parser parser( trivialserialize::parser::tag_caller_must_keep_this_string_valid() , data );
	auto magic_marker = parser.pop_bytes_n(3);
	if (magic_marker !=  "GMK") throw

		std::runtime_error( string("Format incorrect: bad magic marker for GMK (was")
			+ magic_marker + string(")"));

	auto magic_version = parser.pop_byte_u();

	auto magic_secop = parser.pop_byte_u();
	if (magic_secop != m_type_secop) {
		std::ostringstream oss;
		oss<<"Format error: secop was=" << magic_secop << " but we expected=" << m_type_secop;
		throw std::runtime_error(oss.str());
	}

	if (magic_version == 'a') {
		size_t map_size = parser.pop_integer_uvarint();
		assert( map_size <= 100 ); // TODO(serialize_parser_assert)
		for (size_t map_i=0; map_i<map_size; ++map_i) {
			auto sys_id = t_crypto_system_type_from_ID(  parser.pop_integer_uvarint() );
			_info("sys_id=" << sys_id);
			auto sys_keys = parser.pop_vector_object<TKey>(); // <--- load vector of all keys of this system
			// TODO(r) asert sys_id is a normal expected crypto key type
			this->m_cryptolists_general.at( sys_id ) = sys_keys;
		}
		if (!parser.is_end()) throw std::runtime_error("Format incorrect: extra elements at end");
	}	else throw trivialserialize::format_error_read_invalid_version();
	// TODO(r) check that numbers are sorted and not-repeating; extent exceptions type to report details of problem
	this->m_hash_cached=""; // mark it as dirty
}

template <typename TKey>
void c_multikeys_general<TKey>::datastore_save(const string  & fname) const {
	// TODO need a serialize_bin() that works on, and returns, a locked_string
	_note("Savin key to fname="<<fname);

	std::string serialized_data = serialize_bin();
	_info("Serialized to: " << to_debug(serialized_data));

	switch(m_type_secop) {
		case e_key_type_secop_open: {
			_note("Save this as public key");
			filestorage::save_string(e_filestore_galaxy_ipkeys_pub, fname, serialized_data);
			break;
		}
		case e_key_type_secop_secret: {
			_note("Save this as PRIVATE key!!!");
			locked_string data = locked_string::unsafe_create(serialized_data);
			filestorage::save_string_mlocked(e_filestore_wallet_galaxy_ipkeys_PRV, fname, data);
			break;
		}
	}

	if (m_type_secop == e_key_type_secop_open) 	if (m_type_secop == e_key_type_secop_secret) _note("Save this as PRIVATE key!!!");
}

template <typename TKey>
void c_multikeys_general<TKey>::datastore_load(const string  & fname) {
	std::string data;
	locked_string buff_safe;

	switch(m_type_secop) {
		case e_key_type_secop_open: {
			_note("Load this as public key");
			data = filestorage::load_string(e_filestore_galaxy_ipkeys_pub, fname);
			clear();
			_info("Loading: reading now");
			load_from_bin(data);
			break;
		}
		case e_key_type_secop_secret: {
			_note("Load this as PRIVATE key!!!");
			buff_safe = filestorage::load_string_mlocked(e_filestore_wallet_galaxy_ipkeys_PRV, fname);
			clear();
			_info("Loading: reading now");
			load_from_bin(buff_safe.get_string());
			break;
		}
	}
	_info("Loading: done, debug: " << to_debug(serialize_bin()));
}

template <typename TKey>
void c_multikeys_general<TKey>::clear() {
	for (auto & sys : m_cryptolists_general) sys.clear();
}



// ==================================================================
// c_multikeys_pub

c_multikeys_pub::c_multikeys_pub()
	: c_multikeys_general<c_crypto_system::t_pubkey>( e_key_type_secop_open )
{ }

void c_multikeys_pub::add_public(t_crypto_system_type crypto_type, const t_key & key) {
	add_key(crypto_type, key);
}

c_multikeys_pub::t_key c_multikeys_pub::get_public(t_crypto_system_type crypto_type, size_t number_of_key) const {
	return get_key(crypto_type, number_of_key);
}



// ==================================================================
// c_multikeys_PRV


c_multikeys_PRV::c_multikeys_PRV()
	: c_multikeys_general<c_crypto_system::t_PRVkey>( e_key_type_secop_secret )
{ }

void c_multikeys_PRV::add_PRIVATE(t_crypto_system_type crypto_type, const t_key & key) {
	add_key(crypto_type, key);
}

c_multikeys_PRV::t_key c_multikeys_PRV::get_PRIVATE(t_crypto_system_type crypto_type, size_t number_of_key) const {
	return get_key(crypto_type, number_of_key);
}



// ==================================================================
// c_multikeys_PAIR

void c_multikeys_PAIR::debug() const {
	_info("KEY PAIR:");
	_info("(this debug currently disabled");
	/*
	TODO(j) rewrite using new API like get_count_of_systems / get_count_keys_in_system / get_public / get_PRIVATE
	for (unsigned long ix=0; ix<m_pub.m_cryptolists_pubkey.size(); ++ix) {
		const auto & pubkeys_of_this_system  = m_pub. m_cryptolists_pubkey. at(ix);
		const auto & PRVkeys_of_this_system = m_PRV.m_cryptolists_PRVkey.at(ix);
		_info("Cryptosystem: " << t_crypto_system_type_to_name(ix) );
		for(size_t iy=0; iy < pubkeys_of_this_system.size(); ++iy){
			_info("  PUB:" << to_debug( pubkeys_of_this_system.at(iy) ) );
			_info("  PRV:" << to_debug_locked( PRVkeys_of_this_system.at(iy) ) << "\n");
		}
	}*/
	_info("---------");
}

void c_multikeys_PAIR::datastore_save_PRV_and_pub(const string  & fname_base) const {
	m_PRV.datastore_save(fname_base);
	m_pub.datastore_save(fname_base);
}
void c_multikeys_PAIR::datastore_save_pub(const string  & fname_base) const {
	m_pub.datastore_save(fname_base);
}
void c_multikeys_PAIR::datastore_load_PRV_and_pub(const string  & fname_base) {
	m_PRV.datastore_load(fname_base);
	m_pub.datastore_load(fname_base);
}

c_multikeys_PAIR & c_crypto_tunnel::get_IDe() {
	return * PTR(m_IDe);
}

template <typename T_ok_value, typename T_errcode>
void errcode_valid_or_throw( T_ok_value ok_value_raw , T_errcode errcode ,
	const std::string &api_name, const std::string &info="")
{
	// convert type type of ok_value to exactly same type as type of the error code
	// because sometimes they differ, e.g. some APIs have functions return uint32_t,
	// while just doing #define NTRU_OK 0 which is an "int" type.
	const T_errcode ok_value = boost::numeric_cast<T_errcode>( ok_value_raw );
	// the conversion above is safe (range-checked) so we can now safely compare the value:
	if ( errcode != ok_value ) {
		ostringstream oss; oss<<api_name<<" function failed (" << errcode << ")";
		if (info.size()) oss<<" - " << info ;
		oss << '!';
		throw std::runtime_error(oss.str());
	}
}

void NTRU_DRBG_exec_or_throw( uint32_t errcode , const std::string &info="") {
	errcode_valid_or_throw( DRBG_OK, errcode, "DRBG for NTRU", info);
}

void NTRU_exec_or_throw( uint32_t errcode , const std::string &info="") {
	errcode_valid_or_throw( NTRU_OK, errcode, "NTRU", info);
}

void c_multikeys_PAIR::generate(t_crypto_system_count cryptolists_count, bool will_asymkex) {
	_info("Generating from cryptolists_count");
	for (size_t sys=0; sys<cryptolists_count.size(); ++sys) { // all key crypto systems
		// for given crypto system:
		auto sys_id = int_to_enum<t_crypto_system_type>(sys); // ID of this crypto system
		auto how_many = cryptolists_count.at(sys);
		if (how_many > 0) {
			_info("Generate keys " << t_crypto_system_type_to_name(sys_id) << " in amount: "<<how_many);
			if ( will_asymkex || false==t_crypto_system_type_is_asymkex(sys_id) ) {
				this->generate(sys_id, how_many);
			} else _dbg1("Skipping because asymkex; " << to_string(sys_id));
		}
	}
}

void c_multikeys_PAIR::generate() {
	_info("generate X25519");
	generate( e_crypto_system_type_X25519 , 1 );
	_info("generate NTRU");
	generate( e_crypto_system_type_NTRU_EES439EP1 , 1 );
	_info("generate SIDH");
	generate( e_crypto_system_type_SIDH , 0 );
}

std::pair<sodiumpp::locked_string, string> c_multikeys_PAIR::generate_x25519_key_pair() {
	size_t s = crypto_scalarmult_SCALARBYTES;
	sodiumpp::randombytes_locked(s);
	auto rnd = sodiumpp::randombytes_locked(s);
	//_info("Random data size=" << (rnd.size()) );
	//_info("Random data=" << to_debug_locked(rnd) );
	sodiumpp::locked_string key_PRV(rnd); // random secret key
	std::string key_pub( sodiumpp::generate_pubkey_from_privkey(key_PRV) ); // PRV -> pub
	return std::make_pair(std::move(key_PRV), std::move(key_pub));
}

std::pair<sodiumpp::locked_string, string> c_multikeys_PAIR::generate_nrtu_key_pair() {
	// generate key pair
	uint16_t public_key_len = 0, private_key_len = 0;
	// get size of keys:
	NTRU_exec_or_throw(
		ntru_crypto_ntru_encrypt_keygen(
			get_DRBG(128),
			NTRU_EES439EP1,
			&public_key_len, nullptr, &private_key_len, nullptr
			)
		,"generate keypair - get key length"
	);
	// values for NTRU_EES439EP1
	assert(public_key_len == 609);
	assert(private_key_len == 659);

	std::string public_key(public_key_len, 0);
	locked_string private_key(private_key_len);

	NTRU_exec_or_throw(
		ntru_crypto_ntru_encrypt_keygen(get_DRBG(128), NTRU_EES439EP1,
			&public_key_len, reinterpret_cast<uint8_t*>(&public_key[0]),
			&private_key_len, reinterpret_cast<uint8_t*>(private_key.buffer_writable())
		)
		,"generate keypair"
	);

	return std::make_pair(std::move(private_key), std::move(public_key));
}

std::pair<sodiumpp::locked_string, string> c_multikeys_PAIR::generate_sidh_key_pair()
{
	//_info("SIDH generating...");
	PCurveIsogenyStaticData curveIsogenyData = &CurveIsogeny_SIDHp751;
	size_t obytes = (curveIsogenyData->owordbits + 7)/8; // Number of bytes in an element in [1, order]
	size_t pbytes = (curveIsogenyData->pwordbits + 7)/8; // Number of bytes in a field element
	const size_t private_key_len = obytes;
	const size_t public_key_len = 4*2*pbytes;
	locked_string private_key_a(private_key_len);
	locked_string private_key_b(private_key_len);
	std::string public_key_a(public_key_len, 0);
	std::string public_key_b(public_key_len, 0);
	CRYPTO_STATUS status = CRYPTO_SUCCESS;
	PCurveIsogenyStruct curveIsogeny = SIDH_curve_allocate(curveIsogenyData);
	try {
		if (curveIsogeny == nullptr) throw std::runtime_error("SIDH_curve_allocate error");
		status = SIDH_curve_initialize(curveIsogeny, &random_bytes_sidh, curveIsogenyData);
		// generate keys
		status = KeyGeneration_A(
			reinterpret_cast<unsigned char*>(&private_key_a[0]),
			reinterpret_cast<unsigned char *>(&public_key_a[0]),
			curveIsogeny);
		if (status != CRYPTO_SUCCESS) throw std::runtime_error("private key generate error (A)");
		status = KeyGeneration_B(
			reinterpret_cast<unsigned char*>(&private_key_b[0]),
			reinterpret_cast<unsigned char *>(&public_key_b[0]),
			curveIsogeny);
		if (status != CRYPTO_SUCCESS) throw std::runtime_error("private key generate error (B)");

		// check keys valid
		//_info("SIDH validate...");
		bool valid_pub_key = false;
		status = Validate_PKA(
		reinterpret_cast<unsigned char *>(&public_key_a[0]),
			&valid_pub_key,
			curveIsogeny);
		if (status != CRYPTO_SUCCESS) throw std::runtime_error("validate public key error (A)");
		if (!valid_pub_key) throw std::runtime_error("public key (A) is not valid");
		status = Validate_PKB(
		reinterpret_cast<unsigned char *>(&public_key_b[0]),
			&valid_pub_key,
			curveIsogeny);
		if (status != CRYPTO_SUCCESS) throw std::runtime_error("validate public key error (B)");
		if (!valid_pub_key) throw std::runtime_error("public key (B) is not valid");
		assert(public_key_a != public_key_b);
		assert(private_key_a != private_key_b);
	}
	catch(const std::exception &e) {
		SIDH_curve_free(curveIsogeny);
		clear_words(static_cast<void*>(&private_key_a[0]), NBYTES_TO_NWORDS(private_key_len));
		clear_words(static_cast<void*>(&private_key_b[0]), NBYTES_TO_NWORDS(private_key_len));
		clear_words(static_cast<void*>(&public_key_a[0]), NBYTES_TO_NWORDS(public_key_len));
		clear_words(static_cast<void*>(&public_key_b[0]), NBYTES_TO_NWORDS(public_key_len));
		throw e;
	}
	SIDH_curve_free(curveIsogeny);
	locked_string private_key_main(2 * private_key_len);
	std::copy_n(private_key_a.begin(), private_key_len, private_key_main.begin());
	std::copy_n(private_key_b.begin(), private_key_len, private_key_main.begin() + private_key_len);
	std::string public_key_main = public_key_a + public_key_b;
	return std::make_pair(std::move(private_key_main), std::move(public_key_main));
}

DRBG_HANDLE get_DRBG(size_t size) {
	// TODO(r) use std::once / lock? - not thread safe now
	static map<size_t , DRBG_HANDLE> drbg_tab;

	auto found = drbg_tab.find(size);
	if (found == drbg_tab.end()) { // not created yet
		try {
			_note("Creating DRBG for size=" << size);
			DRBG_HANDLE newone;
			NTRU_DRBG_exec_or_throw(
				ntru_crypto_drbg_instantiate(size, nullptr, 0, get_entropy, &newone)
				,"random init"
			);
			drbg_tab[ size ] = newone;
			_note("Creating DRBG for size=" << size << " - ready, as drgb handler=" << newone);
			return newone;
		} catch(...) {
			_erro("Can not init DRBG! (exception)");
			throw;
		}
	} // not found
	else {
		return found->second;
	}
	assert(false);
}

void c_multikeys_PAIR::generate(t_crypto_system_type crypto_system_type, int count) {
	if (!count) return;

	switch (crypto_system_type)
	{
		case e_crypto_system_type_X25519:
		{
			for (int i=0; i<count; ++i) {
				auto keypair = generate_x25519_key_pair();
				this->add_public_and_PRIVATE( crypto_system_type , keypair.second , keypair.first );
			}
			break;
		}

		case e_crypto_system_type_NTRU_EES439EP1:
		{
			for (int i=0; i<count; ++i) {
				auto keypair = generate_nrtu_key_pair();
				this->add_public_and_PRIVATE( crypto_system_type , keypair.second , keypair.first );
			}
			break;
		}

		case e_crypto_system_type_SIDH :
		{
			for (int i=0; i<count; ++i) {
				auto keypair = generate_sidh_key_pair();
				this->add_public_and_PRIVATE( crypto_system_type , keypair.second , keypair.first );
			}
			break;
		} // case

		default: throw runtime_error("Trying to generate unsupported key type:"
			+ t_crypto_system_type_to_name(crypto_system_type));
	} // switch

	string serialized = this->m_pub.serialize_bin();
	_info("Serialized pubkeys: [" << to_debug(serialized) << "]");
}

void c_multikeys_PAIR::add_public_and_PRIVATE(t_crypto_system_type crypto_type,
	 const c_crypto_system::t_pubkey & pubkey ,
	 const c_crypto_system::t_PRVkey & PRVkey)
{
	m_pub.add_public(crypto_type, pubkey);
	_info("Adding PRIVATE KEY: " << to_debug_locked(PRVkey));
	m_PRV.add_PRIVATE(crypto_type, PRVkey);
	//_note("ADD PRIVATE KEY: RESULT IS: " << to_debug(m_PRV.serialize_bin()));
}


// ==================================================================


c_stream::c_stream(bool side_initiator)
:
	m_KCT( return_empty_K() ),
	m_nonce_odd( 0 ),
	m_side_initiator( side_initiator ),
	m_boxer( nullptr ),
	m_unboxer( nullptr )
{
}

sodiumpp::locked_string c_stream::return_empty_K() {
	sodiumpp::locked_string ret( Hash1_size() );
	std::fill_n( ret.buffer_writable() , ret.size() , static_cast<char>(0));
	assert( ret.size() == Hash1_size() );
	return ret;
}


// ---------------------------------------------------------------------------

std::string c_stream::box(const std::string & msg) {
	_dbg2("Boxing as: nonce="<<to_debug(m_boxer.get_nonce().get().to_binary())
	<< " and nonce_cost = " << to_debug(m_boxer.get_nonce_constant().to_binary()) );
	return PTR(m_boxer)->box(msg).to_binary();
}

std::string c_stream::unbox(const std::string & msg) {
	_dbg2("Unboxing as: nonce="<<to_debug(m_boxer.get_nonce().get().to_binary()));
	return PTR(m_unboxer)->unbox(sodiumpp::encoded_bytes(msg , sodiumpp::encoding::binary));
}

// ---------------------------------------------------------------------------

t_crypto_system_count c_stream::get_cryptolists_count_for_KCTf() const {
	return m_cryptolists_count;
}

void c_stream::exchange_start(const c_multikeys_PAIR & ID_self,  const c_multikeys_pub & ID_them,
	bool will_new_id)
{
	_note("EXCHANGE START");
	m_KCT = calculate_KCT( ID_self , ID_them, will_new_id, "" );

	sodiumpp::encoded_bytes nonce_zero =
		sodiumpp::encoded_bytes( string( t_crypto_nonce::constantbytes , char(0)), sodiumpp::encoding::binary)
	;

	m_boxer   = make_unique<t_boxer>  ( sodiumpp::boxer_base::boxer_type_shared_key(),   m_nonce_odd, m_KCT, nonce_zero );
	m_unboxer = make_unique<t_unboxer>( sodiumpp::boxer_base::boxer_type_shared_key(), ! m_nonce_odd, m_KCT, nonce_zero );

	_note("EXCHANGE start:: Stream Crypto prepared with m_nonce_odd=" << m_nonce_odd
		<< " and m_KCT=" << to_debug_locked( m_KCT )
		);
	_dbg1("EXCHANGE start: created boxer   with nonce=" << to_debug(PTR(m_boxer)  ->get_nonce().get().to_binary()));
	_dbg1("EXCHANGE start: created unboxer with nonce=" << to_debug(PTR(m_unboxer)->get_nonce().get().to_binary()));

	assert(m_boxer); assert(m_unboxer);
}

void c_stream::exchange_done(const c_multikeys_PAIR & ID_self,  const c_multikeys_pub & ID_them,
			const std::string & packetstart)
{
	UNUSED(ID_self); UNUSED(ID_them); UNUSED(packetstart);
	TODOCODE;
}

unique_ptr<c_multikeys_PAIR> c_stream::create_IDe(bool will_asymkex) {
	unique_ptr<c_multikeys_PAIR> IDe = make_unique< c_multikeys_PAIR >(); 
	IDe -> generate( m_cryptolists_count , will_asymkex );
	return std::move(IDe);
}

std::string c_stream::exchange_start_get_packet() const {
	TODOCODE;
	return "TODO";
}

// ---------------------------------------------------------------------------

bool c_stream::calculate_nonce_odd(const c_multikeys_PAIR & self,  const c_multikeys_pub & them) {
	return self.m_pub > them;
}

t_crypto_system_type c_stream::get_system_type() const
{
	TODOCODE;	return t_crypto_system_type(0);
}

// ---------------------------------------------------------------------------




// ---------------------------------------------------------------------------

namespace ntru_cpp {

/*
NTRUCALL
ntru_crypto_ntru_encrypt(
    DRBG_HANDLE     drbg_handle,     //     in - handle for DRBG
    uint16_t        pubkey_blob_len, //     in - no. of octets in public key blob
    uint8_t const  *pubkey_blob,     //     in - pointer to public key
    uint16_t        pt_len,          //     in - no. of octets in plaintext
    uint8_t const  *pt,              //     in - pointer to plaintext
    uint16_t       *ct_len,          // in/out - no. of octets in ct, addr for no. of octets in ciphertext
    uint8_t        *ct);             //    out - address for ciphertext
*/

/***
 * Encrypt plain text for given pubkey.
 */
std::string ntru_encrypt(const sodiumpp::locked_string plain, const std::string & pubkey) {
	uint16_t cyphertext_size=0;

	const auto & drbg = get_DRBG(128);

	// first run just to get the size of output:
	ntru_crypto_ntru_encrypt( drbg,
		numeric_cast<uint16_t>(pubkey.size()), reinterpret_cast<const uint8_t*>(pubkey.c_str()),
		numeric_cast<uint16_t>(plain.size()),  reinterpret_cast<const uint8_t*>(plain.c_str()),
		&cyphertext_size, NULL	);
	assert( (cyphertext_size!=0) || (plain.size()==0) );
	assert( (cyphertext_size >= plain.size()) );

	string ret( cyphertext_size , static_cast<char>(0) ); // allocate memory of the encrypted text
	assert( ret.size() == cyphertext_size );
	// actually encrypt now:
	ntru_crypto_ntru_encrypt( drbg,
		numeric_cast<uint16_t>(pubkey.size()), reinterpret_cast<const uint8_t*>(pubkey.c_str()),
		numeric_cast<uint16_t>(plain.size()), reinterpret_cast<const uint8_t*>(plain.c_str()),
		&cyphertext_size, reinterpret_cast<uint8_t*>(&ret[0])	);

	return ret;
}

/*
NTRUCALL
ntru_crypto_ntru_decrypt(
    uint16_t       privkey_blob_len, //     in - no. of octets in private key
                                                 blob 
    uint8_t const *privkey_blob,     //     in - pointer to private key 
    uint16_t       ct_len,           //     in - no. of octets in ciphertext 
    uint8_t const *ct,               //     in - pointer to ciphertext 
    uint16_t      *pt_len,           // in/out - no. of octets in pt, addr for
                                                 no. of octets in plaintext 
    uint8_t       *pt);              //    out - address for plaintext 
*/
sodiumpp::locked_string ntru_decrypt(const string cyphertext, const sodiumpp::locked_string & PRVkey) {
	uint16_t cleartext_len=0;
	ntru_crypto_ntru_decrypt( 
		numeric_cast<uint16_t>(PRVkey.size()), reinterpret_cast<const uint8_t*>(PRVkey.c_str()),
		numeric_cast<uint16_t>(cyphertext.size()), reinterpret_cast<const uint8_t*>(cyphertext.c_str()),
		&cleartext_len, NULL);
	assert( (cleartext_len!=0) || (cyphertext.size()==0) );

	sodiumpp::locked_string ret( cleartext_len );
	assert( ret.size() == cleartext_len );
	ntru_crypto_ntru_decrypt( 
		numeric_cast<uint16_t>(PRVkey.size()), reinterpret_cast<const uint8_t*>(PRVkey.c_str()),
		numeric_cast<uint16_t>(cyphertext.size()), reinterpret_cast<const uint8_t*>(cyphertext.c_str()),
		&cleartext_len, reinterpret_cast<uint8_t*>(ret.buffer_writable()));

	return ret;
}

} // namespace ntru_cpp

c_crypto_system::t_symkey c_stream::calculate_KCT
(const c_multikeys_PAIR & self, const c_multikeys_pub & them , bool will_new_id
, const std::string & packetstart )
{
	UNUSED(packetstart);
	//assert( self.m_pub.get_count_of_systems() == them.m_pub.get_count_keys_in_system() );
	assert(self.m_PRV.get_count_of_systems() == them.get_count_of_systems());
	// TODO assert self priv == them priv;
	assert(self.m_PRV.get_count_of_systems() == self.m_pub.get_count_of_systems());
	// TODO priv self == pub self

	bool should_count = will_new_id;

	if (should_count) for (auto & count : m_cryptolists_count) count=0;

	// fill it with 0 bytes (octets):
	locked_string KCT_accum( Hash1_size() );
	for (size_t p=0; p<KCT_accum.size(); ++p) KCT_accum[p] = static_cast<unsigned char>(0);
	// TODO(rob): we could make locked_string(size_t, char) constructor and use it

	for (size_t sys=0; sys<self.m_pub.get_count_of_systems(); ++sys) { // all key crypto systems
		// for given crypto system:

		auto sys_id = int_to_enum<t_crypto_system_type>(sys); // ID of this crypto system

		const c_multikeys_pub  & self_pub = self.m_pub ; // my    pub keys - all of this sys
		const c_multikeys_PRV & self_PRV = self.m_PRV; // my    PRV keys - all of this sys
		const c_multikeys_pub  & them_pub = them       ; // their pub keys - all of this sys

		auto key_count_a = self_pub.get_count_keys_in_system(sys_id);
		auto key_count_b = them_pub.get_count_keys_in_system(sys_id);
		auto key_count_bigger = std::max( key_count_a , key_count_b );

		if (key_count_bigger < 1) continue ; // !
		if (!( (key_count_a>0) && (key_count_b>0) )) continue ; // !


		if (should_count) m_cryptolists_count.at(sys) = 1; // count that we use this cryptosystem

		if (sys == e_crypto_system_type_X25519) {
			_info("Will do kex in sys="<<t_crypto_system_type_to_name(sys)
				<<" between key counts: " << key_count_a << " -VS- " << key_count_b );

			for (decltype(key_count_bigger) keynr_i=0; keynr_i<key_count_bigger; ++keynr_i) {
				// if we run out of keys then wrap them around. this happens if e.g. we (self) have more keys then them
				auto keynr_a = keynr_i % key_count_a;
				auto keynr_b = keynr_i % key_count_b;
				_info("kex " << keynr_a << " " << keynr_b);

				auto const key_A_pub = self_pub.get_public (sys_id, keynr_a);
				auto const key_A_PRV = self_PRV.get_PRIVATE(sys_id, keynr_a);
				auto const key_B_pub = them_pub.get_public (sys_id, keynr_b); // number b!

				_note("Keys:");
				_info(to_debug_locked_maybe(key_A_pub));
				_info(to_debug_locked_maybe(key_A_PRV));
				_info(to_debug_locked_maybe(key_B_pub));

				using namespace string_binary_op; // operator^

				// a raw key from DH exchange. NOT SECURE yet (uneven distribution), fixed below
				locked_string k_dh_raw( sodiumpp::key_agreement_locked( key_A_PRV, key_B_pub ) ); // *** DH key agreement (part1)
				_info("k_dh_raw = " << to_debug_locked(k_dh_raw) ); // _info( XVAR(k_dh_raw ) );

				locked_string k_dh_agreed = // the fully agreed key, that is secure result of DH
				Hash1_PRV(
					Hash1_PRV( k_dh_raw )
					^	Hash1( key_A_pub )
					^ Hash1( key_B_pub )
				);
				_info("k_dh_agreed = " << to_debug_locked(k_dh_agreed) );

				KCT_accum = KCT_accum ^ k_dh_agreed; // join this fully agreed key, with other keys
				_info("KCT_accum = " <<  to_debug_locked( KCT_accum ) );
			}
		} // X25519



		#if 0
		// TODO MERGEME
		if (sys == e_crypto_system_type_NTRU_EES439EP1) {
			_info("Will do kex in sys="<<t_crypto_system_type_to_name(sys)
				<<" between key counts: " << key_count_a << " -VS- " << key_count_b );

			for (decltype(key_count_bigger) keynr_i=0; keynr_i<key_count_bigger; ++keynr_i) {
				// if we run out of keys then wrap them around. this happens if e.g. we (self) have more keys then them
				auto keynr_a = keynr_i % key_count_a;
				auto keynr_b = keynr_i % key_count_b;
				_info("kex " << keynr_a << " " << keynr_b);

				auto const key_A_pub = self_pub.get_public (sys_id, keynr_a);
				auto const key_A_PRV = self_PRV.get_PRIVATE(sys_id, keynr_a);
				auto const key_B_pub = them_pub.get_public (sys_id, keynr_b); // number b!

				using namespace string_binary_op; // operator^

				if (m_side_initiator) {
					// I am initiator - so I create random passwords, and encrypt them for other side of stream
					uint16_t ciphertext_len = 0;
					// calculate ciphet text size
					_dbg2("calculate ciphertext_len");
					NTRU_exec_or_throw (
						ntru_crypto_ntru_encrypt(get_DRBG(128),
							key_B_pub.size(), reinterpret_cast<const uint8_t *>(key_B_pub.data()), // encrypt to this pubkey
							0, nullptr,
							&ciphertext_len, nullptr) // save result here0
					); //NTRU_exec_or_throw
					_dbg1("ciphertext_len = " << ciphertext_len);

//					sodiumpp::locked_string password_cleartext 
//						= sodiumpp::randombytes_locked(ciphertext_len)); // <--- generate password

					m_ntru_kex_password.

					// encrypt random bytes
					_dbg2("encrypt");
					_dbg2("public key size " << key_B_pub.size());
					
					NTRU_exec_or_throw (
						ntru_crypto_ntru_encrypt(get_DRBG(128),
							key_B_pub.size(), reinterpret_cast<const uint8_t *>(key_B_pub.data()),
							password_cleartext.size(), reinterpret_cast<const uint8_t *>(password_cleartext.c_str()),
							&ciphertext_len, reinterpret_cast<uint8_t *>(&u[0]))
					); // NTRU_exec_or_throw
					assert(ciphertext_len == encrypted_rand_data.size());
					_dbg1("random data encrypted");
					m_ntru_kex_password.push_back(encrypted_rand_data);

					locked_string k_dh_agreed = // the fully agreed key, that is secure result of DH
					Hash1_PRV(
						Hash1_PRV( m_ntru_kex_password )
						^	Hash1( key_A_pub )
						^ Hash1( key_B_pub )
					);
					_info("k_dh_agreed = " << to_debug_locked(k_dh_agreed) );

					KCT_accum = KCT_accum ^ k_dh_agreed; // join this fully agreed key, with other keys
				}
				// them encrypt rand data to me
				else {
					//rc = ntru_crypto_ntru_decrypt(private_key_len, private_key, ciphertext_len,
					//ciphertext, &plaintext_len, NULL);
					uint16_t plaintext_len = 0;
					std::string ciphertext = ntru_rand_encrypt_to_me.front();
					ntru_rand_encrypt_to_me.erase(ntru_rand_encrypt_to_me.begin());
					// calculate plaintext size
					/*NTRU_exec_or_throw (
						ntru_crypto_ntru_decrypt(key_A_PRV.size(), reinterpret_cast<const uint8_t *>(key_A_PRV.data()),
							ciphertext.size(), reinterpret_cast<const uint8_t *>(&ciphertext[0]),
							&plaintext_len, nullptr)
					);*/
					plaintext_len = m_ntru_kex_password.size();
					sodiumpp::locked_string decrypted_rand(plaintext_len);
					_dbg1("plaintext len = " << plaintext_len);
					// decrypt
					NTRU_exec_or_throw (
						ntru_crypto_ntru_decrypt(key_A_PRV.size(), reinterpret_cast<const uint8_t *>(key_A_PRV.data()),
							ciphertext.size(), reinterpret_cast<const uint8_t *>(&ciphertext[0]),
							&plaintext_len, reinterpret_cast<uint8_t *>(&decrypted_rand[0]))
					);
					// TODO double code
					locked_string k_dh_agreed = // the fully agreed key, that is secure result of DH
					Hash1_PRV(
						Hash1_PRV( decrypted_rand )
						^	Hash1( key_A_pub )
						^ Hash1( key_B_pub )
					);
					_info("k_dh_agreed = " << to_debug_locked(k_dh_agreed) );

					KCT_accum = KCT_accum ^ k_dh_agreed; // join this fully agreed key, with other keys

				}
				_info("KCT_accum = " <<  to_debug_locked( KCT_accum ) );
			}
		} // NTRU_EES439EP1
		#endif


		if (sys == e_crypto_system_type_SIDH) {
			_info("Will do kex in sys="<<t_crypto_system_type_to_name(sys)
				<<" between key counts: " << key_count_a << " -VS- " << key_count_b );
			for (decltype(key_count_bigger) keynr_i=0; keynr_i<key_count_bigger; ++keynr_i) {
				// if we run out of keys then wrap them around. this happens if e.g. we (self) have more keys then them
				auto keynr_a = keynr_i % key_count_a;
				auto keynr_b = keynr_i % key_count_b;
				_info("kex " << keynr_a << " " << keynr_b);
				auto const key_self_pub = self_pub.get_public (sys_id, keynr_a);
				auto const key_self_PRV = self_PRV.get_PRIVATE(sys_id, keynr_a);
				auto const key_them_pub = them_pub.get_public (sys_id, keynr_b); // number b!

				//string key_self_pub_a = key_self_pub.substr(0, key_self_pub.size()/2);
				//string key_self_pub_b = key_self_pub.substr(key_self_pub.size()/2);
				string key_them_pub_a = key_them_pub.substr(0, key_them_pub.size()/2);
				string key_them_pub_b = key_them_pub.substr(key_self_pub.size()/2);
				assert( key_self_pub.size() == key_them_pub.size());

				sodiumpp::locked_string key_self_PRV_a(key_self_PRV.size() / 2);
				sodiumpp::locked_string key_self_PRV_b(key_self_PRV.size() / 2);
				// this all also assumes that type-A and type-B private keys have size? is this correct? --rafal TODO(rob)
				std::copy_n(key_self_PRV.begin(), key_self_PRV_a.size(), key_self_PRV_a.begin());
				auto pos_iterator = key_self_PRV.begin() + (key_self_PRV_b.size() / 2);
				std::copy_n(pos_iterator, key_self_PRV_b.size(), key_self_PRV_b.begin());


				// TODO(rob) make this size-calculations more explained; are they correctd?
				// XXX TODO(rob) there was memory out-of-bounds in demo of SIDH by MS it seems. --rafal
				const size_t shared_secret_size = ((CurveIsogeny_SIDHp751.pwordbits + 7)/8) * 2;
				sodiumpp::locked_string shared_secret_a(shared_secret_size);
				sodiumpp::locked_string shared_secret_b(shared_secret_size);
				CRYPTO_STATUS status = CRYPTO_SUCCESS;
				// allocate curve
				// TODO move this to class or make global variable
				PCurveIsogenyStaticData curveIsogenyData = &CurveIsogeny_SIDHp751;
				PCurveIsogenyStruct curveIsogeny = SIDH_curve_allocate(curveIsogenyData);

				_info("SIDH A: prv: " << to_debug_locked(key_self_PRV_a));
				_info("SIDH B: pub: " << to_debug(key_them_pub_b));

				status = SecretAgreement_A(
					reinterpret_cast<unsigned char *>(&key_self_PRV_a[0]),
					reinterpret_cast<unsigned char *>(&key_them_pub_b[0]),
					reinterpret_cast<unsigned char *>(shared_secret_a.buffer_writable()),
					curveIsogeny);
				if (status != CRYPTO_SUCCESS) throw std::runtime_error("SecretAgreement_A error");
				_note(shared_secret_a.get_string());

				status = SecretAgreement_B(
					reinterpret_cast<unsigned char *>(&key_self_PRV_b[0]),
					reinterpret_cast<unsigned char *>(&key_them_pub_a[0]),
					reinterpret_cast<unsigned char *>(&shared_secret_b[0]),
					curveIsogeny);
				if (status != CRYPTO_SUCCESS) throw std::runtime_error("SecretAgreement_B error");

				_info("SIDH agreed secret A: " << to_debug_locked( shared_secret_a )  );
				_info("SIDH agreed secret B: " << to_debug_locked( shared_secret_b )  );

				using namespace string_binary_op; // operator^
				locked_string k_dh_agreed = // the fully agreed key, that is secure result of DH
				Hash1_PRV(
					Hash1_PRV( shared_secret_a ) ^	Hash1_PRV( shared_secret_b ) // both agreed-shared-keys, hashed
					^ Hash1( key_self_pub )	^	Hash1( key_them_pub ) // and hash of public keys too
				); // and all of this hashed once more
				_info("SIDH secret key: " << to_debug_locked(k_dh_agreed));

				KCT_accum = KCT_accum ^ k_dh_agreed; // join this fully agreed key, with other keys

				// key agreement
			}
		} // SIDH

	}

	t_hash_PRV KCT_ready_full = Hash1_PRV( KCT_accum );
	_info("KCT_ready_full = " << to_debug_locked( KCT_ready_full ) );
	assert( KCT_ready_full.size() >= crypto_secretbox_KEYBYTES ); // assert that we can in fact narrow the hash

	locked_string KCT_ready = substr( KCT_ready_full , crypto_secretbox_KEYBYTES); // narrow it to length of symmetrical key

	_note("KCT ready exchanged: " << to_debug_locked( KCT_ready ) );
	return KCT_ready;
} // calculate_KCT



// ==================================================================

std::string c_crypto_tunnel::box(const std::string & msg) {
	return PTR(m_stream_crypto_final)->box(msg);
}

std::string c_crypto_tunnel::unbox(const std::string & msg) {
	return PTR(m_stream_crypto_final)->unbox(msg);
}

std::string c_crypto_tunnel::box_ab(const std::string & msg) {
	return PTR(m_stream_crypto_ab)->box(msg);
}

std::string c_crypto_tunnel::unbox_ab(const std::string & msg) {
	return PTR(m_stream_crypto_ab)->unbox(msg);
}

// ------------------------------------------------------------------


c_crypto_tunnel::c_crypto_tunnel(const c_multikeys_PAIR & self,  const c_multikeys_pub & them)
	: m_side_initiator(true),
	m_IDe(nullptr), m_stream_crypto_ab(nullptr), m_stream_crypto_final(nullptr)
{
	// m_ntru_kex_password(sodiumpp::randombytes_locked(m_ntru_kex_password_size)) // TODOdel or example
	_note("Creating the crypto tunnel");

	m_stream_crypto_ab = make_unique<c_stream>(m_side_initiator);
	m_stream_crypto_final = nullptr;

	PTR(m_stream_crypto_ab)->exchange_start( self, them , true );
	create_IDe();
}

c_crypto_tunnel::c_crypto_tunnel(const c_multikeys_PAIR & self, const c_multikeys_pub & them,
	const std::string & packetstart )
	: m_side_initiator(false),
	m_IDe(nullptr), m_stream_crypto_ab(nullptr), m_stream_crypto_final(nullptr)
{
	PTR(m_stream_crypto_ab)->exchange_done( self, them , packetstart );
}

// : c_stream(IDC_self, IDC_them, rand_ntru_data, std::vector<std::string>()) // TODOdel

void c_crypto_tunnel::create_IDe() {
	_mark("Creating IDe");
	//m_IDe = make_unique<c_multikeys_PAIR>();
	//m_IDe->generate( PTR(m_stream_crypto_ab)->get_cryptolists_count_for_KCTf() );
	m_IDe = PTR( m_stream_crypto_ab )->create_IDe( m_side_initiator==true  );
	_mark("Creating IDe - DONE");
}

void c_crypto_tunnel::create_CTf(const c_multikeys_pub & IDe_them) {
	UNUSED(IDe_them);
	TODOCODE
	m_stream_crypto_final = make_unique<c_stream>(m_side_initiator);
	// *PTR(m_IDe) , IDe_them);
}


// ##################################################################
// ##################################################################
// tests

namespace unittest {

// !!! WARNING:  most of the tests are now MOVED into googletest, e.g. into test/crypto.cpp !!!


// This will be probably removed soon from here - thugh it's a place to very quickly run some tests
// while you develop them

#define UTASSERT(X) do { if (!(X)) { _warn("Unit test failed!"); return false; } } while(0)
#define UTEQ(X,Y) do { if (!(X == Y)) { _warn("Unit test failed! Values differ: actuall=[" << X << "] vs expected=["<<Y<<"]" ); return false; } } while(0)

class c_symhash_state__tests_with_private_access {
	public:
		static bool foo1();
};

bool c_symhash_state__tests_with_private_access::foo1() {
	return true;
}

bool aeshash_foo2() {
	return true;
}


bool alltests() {
	if (! aeshash_foo2()) return false;
	if (! c_symhash_state__tests_with_private_access::foo1()) return false;
	return true;
}

} // namespace


void test_string_lock() {
	_mark("Testing locked string operations");
	{
		locked_string a(3);
		assert(a.size() == 3);

		a[0]='x';	a[1]='y';	a[2]='z';
		assert(a.size() == 3);
		assert(a.c_str() == string("xyz"));

		char * ptr = a.buffer_writable();
		*(ptr+0) = 'X';		*(ptr+1) = 'Y';		*(ptr+2) = 'Z';
		assert(a.size() == 3);
		assert(a.c_str() == string("XYZ"));
	}
	{
		string s("pqr");
		locked_string a(s);
		assert(a.size() == 3);

		a[0]='x';	a[1]='y';	a[2]='z';
		assert(a.size() == 3);
		assert(a.c_str() == string("xyz"));

		char * ptr = a.buffer_writable();
		*(ptr+0) = 'X';		*(ptr+1) = 'Y';		*(ptr+2) = 'Z';
		assert(a.size() == 3);
		assert(a.c_str() == string("XYZ"));
	}

	vector<locked_string> vec;
	std::string s1("TestString");
	locked_string s = locked_string::move_from_not_locked_string(std::move(s1));
	vec.push_back(s);
	vec.push_back(s);
	vec.push_back(s);
	if (vec.at(2).get_string() != "TestString") throw std::runtime_error("Test failed - vector of locked strings");
//	return;
}

void test_crypto() {

	test_string_lock();

	_mark("Create IDC");

	// Alice: IDC
	c_multikeys_PAIR keypairA;
	keypairA.generate(e_crypto_system_type_X25519,0);
	keypairA.generate(e_crypto_system_type_NTRU_EES439EP1,1);

	if (0) {
		keypairA.datastore_save_PRV_and_pub("alice.key");
		keypairA.datastore_save_PRV_and_pub("alice2.key");

		c_multikeys_PAIR loadedA;
		loadedA.datastore_load_PRV_and_pub("alice.key");
		loadedA.datastore_save_PRV_and_pub("alice.key.again");
	}

	// Bob: IDC
	c_multikeys_PAIR keypairB;
	keypairB.generate(e_crypto_system_type_X25519,3);
	keypairB.generate(e_crypto_system_type_NTRU_EES439EP1,3);

	c_multikeys_pub keypubA = keypairA.m_pub;
	c_multikeys_pub keypubB = keypairB.m_pub;

	if (0) {
		// Check key save/restore (without saving to file)
		string keypubA_serialized = keypubA.serialize_bin();
		c_multikeys_pub keypubA_restored;
		keypubA_restored.load_from_bin( keypubA_serialized );
		_note("Serialize save/load test: serialized key to: " << to_debug(keypubA_serialized));
		if (keypubA.get_hash() == keypubA_restored.get_hash()) {
			_info("Seems to match");
		} else throw std::runtime_error("Test failed serialize save/load");
	}



	// test seding messages in CT sessions

	for (int ib=0; ib<1; ++ib) {
		_note("Starting new conversation (new CT) - number " << ib);

		// Create CT (e.g. CTE?) - that has KCT
		_note("Alice CT:");
		c_crypto_tunnel AliceCT(keypairA, keypubB); // proto!
		_note("Bob CT:");
		c_crypto_tunnel BobCT  (keypairB, keypubA); // proto!
		_mark("Prepared tunnels (KCTab)");

		c_multikeys_pub keypairA_IDe_pub = AliceCT.get_IDe().m_pub;
		c_multikeys_pub keypairB_IDe_pub = BobCT  .get_IDe().m_pub;


/* for NTru:
	// Create CT (e.g. CTE?) - that has KCT
	std::vector<std::string> empty_vactor;
	_note("Alice CT:");
	c_crypto_tunnel AliceCT(keypairA, keypubB, empty_vactor);
	_note("Bob CT:");
	c_crypto_tunnel BobCT  (keypairB, keypubA, AliceCT.get_encrypt_ntru_rand());
	_mark("Prepared tunnels (KCTab)");
*/

		return; // !!!

		_mark("Preparing for ephemeral KEX:");
		_note( to_debug( keypairA_IDe_pub.serialize_bin() ) );
		_note( to_debug( keypairB_IDe_pub.serialize_bin() ) );

		AliceCT.create_CTf( keypairB_IDe_pub );
		BobCT  .create_CTf( keypairA_IDe_pub );


		// generate ephemeral keys

		_warn("WARNING: KCTab - this code is NOT SECURE [also] because it uses SAME NONCE in each dialog, "
			"so each CT between given Alice and Bob will have same crypto key which is not secure!!!");
		for (int ia=0; ia<5; ++ia) {
			_dbg2("Alice will box:");
			auto msg1s = AliceCT.box("Hello");
			auto msg1r = BobCT.unbox(msg1s);
			_note("Decrypted message: [" << msg1r << "] from encrypted: " << to_debug(msg1s));

			auto msg2s = BobCT.box("Hello");
			auto msg2r = AliceCT.unbox(msg2s);
			_note("Decrypted message: [" << msg2r << "] from encrypted: " << to_debug(msg2s));
		}
	}

	return ;


#if 0
	// the goal:
	const string app_msg1("Message-send-from-application"); // the finall end-user text that we want to tunnel.
	string app_msg;
	for (int i=0; i<1; ++i) app_msg += app_msg1;

	// --- in runtime ---

	// X25519 DH exchange
	std::string Alice_dh_sk(sodiumpp::randombytes(crypto_scalarmult_SCALARBYTES)); // random secret key
	std::string Alice_dh_pk(sodiumpp::crypto_scalarmult_base(Alice_dh_sk));

	std::string Bob_dh_sk(sodiumpp::randombytes(crypto_scalarmult_SCALARBYTES));
	std::string Bob_dh_pk(sodiumpp::crypto_scalarmult_base(Bob_dh_sk));

	std::string Alice_dh_shared = sodiumpp::crypto_scalarmult(Alice_dh_sk, Bob_dh_pk);
	std::string Bob_dh_shared = sodiumpp::crypto_scalarmult(Bob_dh_sk, Alice_dh_pk);
	// TODO use generic hash
	if (Alice_dh_shared == Bob_dh_shared) _note("DH shared - OK"); else _erro("key exchange error");

	sodiumpp::nonce<crypto_box_NONCEBYTES> nonce;

	c_crypto_system crypto_system;

	// Alice prepare boxer
	// and xor pubkey_alice xor pubkey_bob TODO? (hash distribution)
	string Alice_dh_key = crypto_system.Hash1( Alice_dh_shared ).substr(0,crypto_secretbox_KEYBYTES);
	_note("Alice encrypts with: " << string_as_dbg(string_as_bin(Alice_dh_key)).get());
	assert( Alice_dh_pk != Bob_dh_pk ); // to avoid any tricks in this corner case when someone sends us back our pubkey
	typedef sodiumpp::nonce64 t_crypto_nonce;
	using sodiumpp::boxer_base;

	sodiumpp::boxer< t_crypto_nonce > Alice_boxer  ( boxer_base::boxer_type_shared_key() ,
	(Alice_dh_pk > Bob_dh_pk) ,
	sodiumpp::encoded_bytes(Alice_dh_key, sodiumpp::encoding::binary)
	);

	sodiumpp::unboxer< t_crypto_nonce > Alice_unboxer( boxer_base::boxer_type_shared_key() , ! (Alice_dh_pk > Bob_dh_pk) , sodiumpp::encoded_bytes(Alice_dh_key, sodiumpp::encoding::binary) );
	_note("Alice boxer nonce: " << string_as_dbg(string_as_bin(Alice_boxer.get_nonce().get().bytes)).get());
	_note("Alice boxer nonce: " << string_as_dbg(string_as_bin(Alice_boxer.get_nonce_constant().bytes)).get());

	auto nonce_constant = Alice_boxer.get_nonce_constant();

	//string encrypt = sodiumpp::crypto_secretbox(app_msg, nonce.get().bytes, Alice_dh_key);

	// Use CryptoAuth:

	auto msg_send = string{"Hello-world"};
	auto cypher = Alice_boxer.box(msg_send);

	_info("send: " << msg_send );
	_info("cyph: " << cypher.to_binary() );


	// alice generate packet:
	trivialserialize::generator gen(50);
	// assert that message size is smaller then around 2^(8*2)

	gen.push_bytes_n(16, nonce_constant.bytes );
	gen.push_bytes_sizeoctets<1>( cypher.to_binary()  );
	gen.push_byte_u(';'); // XXX

	const auto & packet = gen.str();
map_size
	_info("Network packet:" << packet);
	trivialserialize::parser parser( trivialserialize::parser::tag_caller_must_keep_this_string_valid() ,
		packet // !! do not change this while parser exists
	);
	const string Bob_nonce_constant_str = parser.pop_bytes_n(16);
	_info("Parsed: nonce const " << Bob_nonce_constant_str);
	// t_crypto_nonce Bob_nonce_constant( sodiumpp::encoded_bytes( Bob_nonce_constant_str , sodiumpp::encoding::binary ));
	const string Bob_cyphertext = parser.pop_bytes_sizeoctets<1>();
	_info("Parsed: cypher " << Bob_cyphertext);

	// Bob  prepare boxer:
	string Bob_dh_key = crypto_system.Hash1( Bob_dh_shared).substr(0,crypto_secretbox_KEYBYTES);
	_note("Bob decrypts with: " << string_as_dbg(string_as_bin(Bob_dh_key)).get());
	assert( Bob_dh_key != Alice_dh_pk ); // to avoid any tricks in this corner case when someone sends us back our pubkey
	//string decrypt = sodiumpp::crypto_secretbox_open(encrypt, nonce.get().bytes, Bob_dh_key);
	sodiumpp::boxer< t_crypto_nonce > Bob_boxer  ( boxer_base::boxer_type_shared_key() , (Bob_dh_pk > Alice_dh_pk) , sodiumpp::encoded_bytes(Bob_dh_key, sodiumpp::encoding::binary ) );

	sodiumpp::unboxer< t_crypto_nonce > Bob_unboxer(
		boxer_base::boxer_type_shared_key() ,
		! (Bob_dh_pk > Alice_dh_pk) ,
		sodiumpp::encoded_bytes(Bob_dh_key, sodiumpp::encoding::binary),
	  sodiumpp::encoded_bytes( Bob_nonce_constant_str , sodiumpp::encoding::binary)
	);
	_note("Bob unboxer nonce: " << string_as_dbg(string_as_bin(Bob_unboxer.get_nonce().get().bytes)).get());

	try {
		auto msg_recived = Bob_unboxer.unbox( sodiumpp::encoded_bytes(Bob_cyphertext,  sodiumpp::encoding::binary)  );
		_info("reci: " << msg_recived );
	} catch(const std::exception &e) {
		_erro("Failed: " << e.what());
	}

//	if (safe_string_cmp(app_msg,decrypt)) _note("Encrypted message - OK "); else _erro("Msg decoded differs!");
//	if (! safe_string_cmp(encrypt,decrypt)) _note("It is encrypted  - OK"); else _erro("Not encrypted?!");

	// XXX _note("Encrypted as:" << sodiumpp::bin2hex(encrypt));

	return;

#endif

	_mark("Testing crypto - unittests");
	if (! unittest::alltests() ) {
		_erro("Unit tests failed!");
		return ;
	}

	_mark("Testing crypto - more");

	#define SHOW _info( string_as_dbg( string_as_bin( symhash.get_password() ) ).get() );

	c_symhash_state symhash( string_as_hex("6a6b").get() ); // "jk"
	SHOW;
	symhash.next_state();
	SHOW;
	symhash.next_state();
	SHOW;
	symhash.next_state( "---RX-1---" );
	SHOW;
	symhash.next_state();
	SHOW;
	symhash.next_state( "---RX-2---" );
	SHOW;

	// SymHash




	// DH+DH

}

void test_crypto_benchmark(const size_t seconds_for_test_case) {
	_mark("test_crypto_benchmark");

	// X25519
	size_t generated_keys_x25519 = 0;
	auto start_point = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - start_point < std::chrono::seconds(seconds_for_test_case)) {
		auto pair = c_multikeys_PAIR::generate_x25519_key_pair();
		++generated_keys_x25519;
	}
	auto stop_point = std::chrono::steady_clock::now();
	unsigned int x25519_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_point - start_point).count();

	// ntru
	size_t generated_keys_ntru = 0;
	start_point = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - start_point < std::chrono::seconds(seconds_for_test_case)) {
		auto pair = c_multikeys_PAIR::generate_nrtu_key_pair();
		++generated_keys_ntru;
	}
	stop_point = std::chrono::steady_clock::now();
	unsigned int ntru_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_point - start_point).count();

	//sidh
	size_t generated_keys_sidh = 0;
	start_point = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - start_point < std::chrono::seconds(seconds_for_test_case)) {
		auto pair = c_multikeys_PAIR::generate_sidh_key_pair();
		++generated_keys_sidh;
	}
	stop_point = std::chrono::steady_clock::now();
	unsigned int sidh_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stop_point - start_point).count();

	_info("X25519");
	_info("Generated " << generated_keys_x25519 << " in " << x25519_ms << " ms");
	_info(static_cast<double>(generated_keys_x25519) / x25519_ms * 1000 << " key pairs per second");
	_info("NTRU");
	_info("Generated " << generated_keys_ntru << " in " << ntru_ms << " ms");
	_info(static_cast<double>(generated_keys_ntru) / ntru_ms * 1000 << " key pairs per second");
	_info("SIDH");
	_info("Generated " << generated_keys_sidh << " in " << sidh_ms << " ms");
	_info(static_cast<double>(generated_keys_sidh) / sidh_ms * 1000 << " key pairs per second");

}


} // namespace
