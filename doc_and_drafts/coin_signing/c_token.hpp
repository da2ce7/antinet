#ifndef COIN_SIGNING_C_TOKEN_HPP
#define COIN_SIGNING_C_TOKEN_HPP
#include <cstdlib>
#include <vector>
#include <iostream>
#include <limits>
#include <chrono>
#include <algorithm>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>

#include <boost/serialization/binary_object.hpp>

class token_id_generator {
	static size_t id;
  public:
	static size_t generate_id ();
};

struct c_chainsign_element {
    c_chainsign_element () = default;
	c_chainsign_element (const std::string, const std::string, const std::string, const std::string);
    c_chainsign_element (const std::string &);	// deserialize chainelement from packet

    std::string m_msg;
    std::string m_msg_sign;
    std::string m_signer;
    std::string m_signer_pubkey;

    template <typename Archive>
    void serialize(Archive &ar, const unsigned version) {
        ar & m_msg;
        ar & m_msg_sign;
        ar & m_signer;
        ar & m_signer_pubkey;
    }
};

struct c_token {
	std::vector<c_chainsign_element> m_chainsign;

    c_token (long long, std::chrono::time_point<std::chrono::system_clock>, std::chrono::hours);
    c_token (std::string);		///< deserialize token from recived packet

    std::string to_packet();	///< serialize token

    std::string get_emiter_name();
    size_t get_id() const;
    std::chrono::time_point<std::chrono::system_clock>  get_expiration_date();

    long long get_size() const;
    bool check_ps (long long);

    friend class boost::serialization::access;
  private:
    size_t m_id;
    std::chrono::time_point<std::chrono::system_clock> m_expiration_date;
    long long m_password;

    template<typename Archive>
    void serialize(Archive &ar, const unsigned int version)
    {
        ar & m_id;
        ar & boost::serialization::make_binary_object (&m_expiration_date,sizeof(m_expiration_date));
        ar & m_chainsign;
        ar & m_password;
    }
};

bool operator != (const c_chainsign_element &,const c_chainsign_element &);

bool operator == (const c_token &, const c_token &);

bool operator < (const c_token &, const c_token &);

#endif //COIN_SIGNING_C_TOKEN_HPP
