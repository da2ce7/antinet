#ifndef GENERATE_CONFIG_HPP
#define GENERATE_CONFIG_HPP

#include "libs0.hpp"
#include "filestorage.hpp"
#include "crypto/crypto.hpp"


enum class e_crypto_strength : unsigned char {
	lowest = 0,
	fast,
	normal,
	high,
	highest,
	idp_normal
};


class generate_config {
public:
	generate_config() = delete;

	/**
	 * @brief crypto_permanent Create set of permanent keys with ability to sign
	 * @param strength
	 */
	static void crypto_permanent (e_crypto_strength strength);
	/**
	 * @brief crypto_current Create set of current keys with ability to make DN exchange
	 * @param strength
	 */
	static void crypto_current (e_crypto_strength strength);

	static std::string m_crypto_permanent_name;
	static std::string m_crypto_current_name;

	static std::string get_crypto_permanent_name();
	static std::string get_crypto_current_name();

	static void set_crypto_permanent_name(const std::string &crypto_permanent_name);
	static void set_crypto_current_name(const std::string &crypto_current_name);
};

#endif // GENERATE_CONFIG_HPP
