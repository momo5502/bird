#pragma once

#include <string>
#include <tomcrypt.h>

namespace utils::cryptography
{
#ifdef LTC_MECC
	namespace ecc
	{
		class key final
		{
		public:
			key();
			~key();

			key(key&& obj) noexcept;
			key(const key& obj);
			key& operator=(key&& obj) noexcept;
			key& operator=(const key& obj);

			bool is_valid() const;

			ecc_key& get();
			const ecc_key& get() const;

			std::string get_public_key() const;

			void set(const std::string& pub_key_buffer);

			void deserialize(const std::string& key);

			std::string serialize(int type = PK_PRIVATE) const;

			std::string get_openssl() const;
			void set_openssl(const std::string& key);

			void free();

			bool operator==(key& key) const;

			uint64_t get_hash() const;

		private:
			ecc_key key_storage_{};
		};

		key generate_key(int bits);
		key generate_key(int bits, const std::string& entropy);
		std::string sign_message(const key& key, const std::string& message);
		bool verify_message(const key& key, const std::string& message, const std::string& signature);

		bool encrypt(const key& key, std::string& data);
		bool decrypt(const key& key, std::string& data);
	}
#endif

#ifdef LTC_MRSA
	namespace rsa
	{
		std::string encrypt(const std::string& data, const std::string& hash, const std::string& key);
	}
#endif

#ifdef LTC_DES
	namespace des3
	{
		std::string encrypt(const std::string& data, const std::string& iv, const std::string& key);
		std::string decrypt(const std::string& data, const std::string& iv, const std::string& key);
	}
#endif

#ifdef LTC_TIGER
	namespace tiger
	{
		std::string compute(const std::string& data, bool hex = false);
		std::string compute(const uint8_t* data, size_t length, bool hex = false);
	}
#endif

#ifdef LTC_RIJNDAEL
	namespace aes
	{
		std::string encrypt(const std::string& data, const std::string& iv, const std::string& key);
		std::string decrypt(const std::string& data, const std::string& iv, const std::string& key);
	}
#endif

#if defined(LTC_SHA1) && defined(LTC_HMAC)
	namespace hmac_sha1
	{
		std::string compute(const std::string& data, const std::string& key);
	}
#endif

#ifdef LTC_SHA1
	namespace sha1
	{
		std::string compute(const std::string& data, bool hex = false);
		std::string compute(const uint8_t* data, size_t length, bool hex = false);
	}
#endif

#ifdef LTC_SHA256
	namespace sha256
	{
		std::string compute(const std::string& data, bool hex = false);
		std::string compute(const uint8_t* data, size_t length, bool hex = false);
	}
#endif

#ifdef LTC_SHA512
	namespace sha512
	{
		std::string compute(const std::string& data, bool hex = false);
		std::string compute(const uint8_t* data, size_t length, bool hex = false);
	}
#endif

#ifdef LTC_BASE64
	namespace base64
	{
		std::string encode(const uint8_t* data, size_t len);
		std::string encode(const std::string& data);
		std::string decode(const std::string& data);
	}
#endif

	namespace jenkins_one_at_a_time
	{
		unsigned int compute(const std::string& data);
		unsigned int compute(const char* key, size_t len);
	};

	namespace random
	{
		uint32_t get_integer();
		std::string get_challenge();
		void get_data(void* data, size_t size);
	}
}
