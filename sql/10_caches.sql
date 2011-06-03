-- cache tables for user, client etc. information

CREATE TABLE cached_user_info (
	-- user info delivered by the service
	id INTEGER PRIMARY KEY,
	longname VARCHAR NOT NULL,
	screenname VARCHAR UNIQUE NOT NULL,
	created_at TIMESTAMP WITH TIME ZONE,
	profile_image_url VARCHAR,
	protected BOOLEAN NOT NULL,
	verified BOOLEAN NOT NULL,

	-- profile image caching
	profile_image_name VARCHAR,
	profile_image_expires TIMESTAMP WITH TIME ZONE
);
