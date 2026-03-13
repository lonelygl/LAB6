-- ============================================================
--  UFC Database Setup Script (idempotent)
-- ============================================================

-- ROLES
DO $$ BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'ufc_admin') THEN
    CREATE ROLE ufc_admin LOGIN PASSWORD 'admin123';
  END IF;
END $$;

DO $$ BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'ufc_guest') THEN
    CREATE ROLE ufc_guest LOGIN PASSWORD 'guest123';
  END IF;
END $$;

-- TABLES
CREATE TABLE IF NOT EXISTS app_users (
    username  VARCHAR(60) PRIMARY KEY,
    password  VARCHAR(255) NOT NULL,
    role      VARCHAR(10)  NOT NULL CHECK (role IN ('admin','guest'))
);

CREATE TABLE IF NOT EXISTS fights (
    id           SERIAL PRIMARY KEY,
    date         VARCHAR(10)  NOT NULL CHECK (date ~ '^\d{4}-\d{2}-\d{2}$'),
    fight_time   VARCHAR(8)   NOT NULL CHECK (fight_time ~ '^\d{1,2}:[0-5]\d$'),
    event        VARCHAR(100) NOT NULL,
    location     VARCHAR(100) NOT NULL,
    card_type    VARCHAR(30)  NOT NULL CHECK (card_type IN ('Main Card','Preliminary Card','Early Prelims')),
    weight_class VARCHAR(40)  NOT NULL CHECK (weight_class IN (
                    'Flyweight','Bantamweight','Featherweight','Lightweight','Welterweight',
                    'Middleweight','Light Heavyweight','Heavyweight',
                    'Women''s Strawweight','Women''s Flyweight','Women''s Bantamweight','Women''s Featherweight'
                 )),
    fighter_1    VARCHAR(60)  NOT NULL CHECK (fighter_1 !~ '^\d+$' AND length(trim(fighter_1)) > 0),
    fighter_2    VARCHAR(60)  NOT NULL CHECK (fighter_2 !~ '^\d+$' AND length(trim(fighter_2)) > 0),
    winner       VARCHAR(60)  NOT NULL CHECK (winner !~ '^\d+$')
);

ALTER TABLE fights DROP CONSTRAINT IF EXISTS winner_is_fighter;
ALTER TABLE fights ADD CONSTRAINT winner_is_fighter
    CHECK (winner = '' OR winner = fighter_1 OR winner = fighter_2);

-- PERMISSIONS
GRANT USAGE ON SCHEMA public TO ufc_admin, ufc_guest;
GRANT SELECT, INSERT, UPDATE, DELETE ON TABLE fights TO ufc_admin;
GRANT USAGE, SELECT, UPDATE ON SEQUENCE fights_id_seq TO ufc_admin;
GRANT SELECT ON TABLE fights TO ufc_guest;
GRANT SELECT, INSERT ON TABLE app_users TO ufc_admin, ufc_guest;

-- DEFAULT ACCOUNTS
INSERT INTO app_users(username, password, role) VALUES('admin', 'adminpass', 'admin') ON CONFLICT (username) DO NOTHING;
INSERT INTO app_users(username, password, role) VALUES('guest', 'guestpass', 'guest') ON CONFLICT (username) DO NOTHING;

-- (no default test data — import via JSON if needed)

-- STORED PROCEDURES & FUNCTIONS

CREATE OR REPLACE PROCEDURE sp_add_fight(
    p_date VARCHAR, p_fight_time VARCHAR, p_event VARCHAR, p_location VARCHAR,
    p_card_type VARCHAR, p_weight_class VARCHAR,
    p_fighter_1 VARCHAR, p_fighter_2 VARCHAR, p_winner VARCHAR
) LANGUAGE plpgsql AS $BODY$
DECLARE v_dup INT;
BEGIN
    IF NOT (p_fight_time ~ '^\d{1,2}:[0-5]\d$') THEN RAISE EXCEPTION 'fight_time must be MM:SS format'; END IF;
    IF p_card_type NOT IN ('Main Card','Preliminary Card','Early Prelims') THEN RAISE EXCEPTION 'Invalid card_type: %', p_card_type; END IF;
    IF p_weight_class NOT IN ('Flyweight','Bantamweight','Featherweight','Lightweight','Welterweight','Middleweight','Light Heavyweight','Heavyweight','Women''s Strawweight','Women''s Flyweight','Women''s Bantamweight','Women''s Featherweight') THEN RAISE EXCEPTION 'Invalid weight_class: %', p_weight_class; END IF;
    IF p_fighter_1 ~ '^\d+$' OR p_fighter_2 ~ '^\d+$' OR p_winner ~ '^\d+$' THEN RAISE EXCEPTION 'Fighter names must not be numeric-only'; END IF;
    IF p_winner <> '' AND p_winner <> p_fighter_1 AND p_winner <> p_fighter_2 THEN RAISE EXCEPTION 'winner must be fighter_1 or fighter_2'; END IF;
    SELECT COUNT(*) INTO v_dup FROM fights WHERE date=p_date AND fight_time=p_fight_time AND event=p_event AND location=p_location AND fighter_1=p_fighter_1 AND fighter_2=p_fighter_2 AND winner=p_winner AND card_type=p_card_type AND weight_class=p_weight_class;
    IF v_dup > 0 THEN RAISE EXCEPTION 'Duplicate record already exists'; END IF;
    INSERT INTO fights(date,fight_time,event,location,card_type,weight_class,fighter_1,fighter_2,winner) VALUES(p_date,p_fight_time,p_event,p_location,p_card_type,p_weight_class,p_fighter_1,p_fighter_2,p_winner);
END;
$BODY$;

CREATE OR REPLACE PROCEDURE sp_update_fight(
    p_id INT, p_date VARCHAR, p_fight_time VARCHAR, p_event VARCHAR, p_location VARCHAR,
    p_card_type VARCHAR, p_weight_class VARCHAR, p_fighter_1 VARCHAR, p_fighter_2 VARCHAR, p_winner VARCHAR
) LANGUAGE plpgsql AS $BODY$
DECLARE v_dup INT;
BEGIN
    IF NOT EXISTS (SELECT 1 FROM fights WHERE id=p_id) THEN RAISE EXCEPTION 'Record id % not found', p_id; END IF;
    IF NOT (p_fight_time ~ '^\d{1,2}:[0-5]\d$') THEN RAISE EXCEPTION 'fight_time must be MM:SS format'; END IF;
    IF p_card_type NOT IN ('Main Card','Preliminary Card','Early Prelims') THEN RAISE EXCEPTION 'Invalid card_type: %', p_card_type; END IF;
    IF p_weight_class NOT IN ('Flyweight','Bantamweight','Featherweight','Lightweight','Welterweight','Middleweight','Light Heavyweight','Heavyweight','Women''s Strawweight','Women''s Flyweight','Women''s Bantamweight','Women''s Featherweight') THEN RAISE EXCEPTION 'Invalid weight_class: %', p_weight_class; END IF;
    IF p_fighter_1 ~ '^\d+$' OR p_fighter_2 ~ '^\d+$' OR p_winner ~ '^\d+$' THEN RAISE EXCEPTION 'Fighter names must not be numeric-only'; END IF;
    IF p_winner <> '' AND p_winner <> p_fighter_1 AND p_winner <> p_fighter_2 THEN RAISE EXCEPTION 'winner must be fighter_1 or fighter_2'; END IF;
    SELECT COUNT(*) INTO v_dup FROM fights WHERE date=p_date AND fight_time=p_fight_time AND event=p_event AND location=p_location AND fighter_1=p_fighter_1 AND fighter_2=p_fighter_2 AND winner=p_winner AND card_type=p_card_type AND weight_class=p_weight_class AND id<>p_id;
    IF v_dup > 0 THEN RAISE EXCEPTION 'Update would create a duplicate'; END IF;
    UPDATE fights SET date=p_date,fight_time=p_fight_time,event=p_event,location=p_location,card_type=p_card_type,weight_class=p_weight_class,fighter_1=p_fighter_1,fighter_2=p_fighter_2,winner=p_winner WHERE id=p_id;
END;
$BODY$;

CREATE OR REPLACE PROCEDURE sp_delete_by_id(p_id INT) LANGUAGE plpgsql AS $BODY$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM fights WHERE id=p_id) THEN RAISE EXCEPTION 'Record id % not found', p_id; END IF;
    DELETE FROM fights WHERE id=p_id;
END;
$BODY$;

CREATE OR REPLACE PROCEDURE sp_delete_by_event(p_event VARCHAR) LANGUAGE plpgsql AS $BODY$
DECLARE v_count INT;
BEGIN
    SELECT COUNT(*) INTO v_count FROM fights WHERE event = p_event;
    IF v_count = 0 THEN RAISE EXCEPTION 'No records with event = %', p_event; END IF;
    DELETE FROM fights WHERE event = p_event;
END;
$BODY$;

-- Uses DELETE (not TRUNCATE) so ufc_admin does not need table ownership
CREATE OR REPLACE PROCEDURE sp_clear_table() LANGUAGE plpgsql AS $BODY$
BEGIN
    DELETE FROM fights;
END;
$BODY$;

CREATE OR REPLACE FUNCTION fn_get_all()
RETURNS TABLE(id INT, date VARCHAR, fight_time VARCHAR, event VARCHAR, location VARCHAR, card_type VARCHAR, weight_class VARCHAR, fighter_1 VARCHAR, fighter_2 VARCHAR, winner VARCHAR)
LANGUAGE plpgsql AS $BODY$
BEGIN
    RETURN QUERY SELECT f.id,f.date,f.fight_time,f.event,f.location,f.card_type,f.weight_class,f.fighter_1,f.fighter_2,f.winner FROM fights f ORDER BY f.id;
END;
$BODY$;

CREATE OR REPLACE FUNCTION fn_get_by_id(p_id INT)
RETURNS TABLE(id INT, date VARCHAR, fight_time VARCHAR, event VARCHAR, location VARCHAR, card_type VARCHAR, weight_class VARCHAR, fighter_1 VARCHAR, fighter_2 VARCHAR, winner VARCHAR)
LANGUAGE plpgsql AS $BODY$
BEGIN
    RETURN QUERY SELECT f.id,f.date,f.fight_time,f.event,f.location,f.card_type,f.weight_class,f.fighter_1,f.fighter_2,f.winner FROM fights f WHERE f.id=p_id;
END;
$BODY$;

CREATE OR REPLACE FUNCTION fn_search_by_event(p_event VARCHAR)
RETURNS TABLE(id INT, date VARCHAR, fight_time VARCHAR, event VARCHAR, location VARCHAR, card_type VARCHAR, weight_class VARCHAR, fighter_1 VARCHAR, fighter_2 VARCHAR, winner VARCHAR)
LANGUAGE plpgsql AS $BODY$
BEGIN
    RETURN QUERY SELECT f.id,f.date,f.fight_time,f.event,f.location,f.card_type,f.weight_class,f.fighter_1,f.fighter_2,f.winner FROM fights f WHERE f.event ILIKE '%'||p_event||'%' ORDER BY f.id;
END;
$BODY$;

CREATE OR REPLACE FUNCTION fn_app_login(p_username VARCHAR, p_password VARCHAR)
RETURNS VARCHAR LANGUAGE plpgsql AS $BODY$
DECLARE v_role VARCHAR;
BEGIN
    SELECT role INTO v_role FROM app_users WHERE username=p_username AND password=p_password;
    IF NOT FOUND THEN RAISE EXCEPTION 'Invalid username or password'; END IF;
    RETURN v_role;
END;
$BODY$;

CREATE OR REPLACE PROCEDURE sp_app_register(
    p_username VARCHAR, p_password VARCHAR, p_role VARCHAR, p_admin_code VARCHAR DEFAULT ''
) LANGUAGE plpgsql AS $BODY$
BEGIN
    IF p_role NOT IN ('admin','guest') THEN RAISE EXCEPTION 'Role must be admin or guest'; END IF;
    IF length(trim(p_username)) < 3 THEN RAISE EXCEPTION 'Username must be at least 3 characters'; END IF;
    IF length(p_password) <> 8 THEN RAISE EXCEPTION 'Password must be exactly 8 characters'; END IF;
    IF p_role = 'admin' AND p_admin_code <> 'admin' THEN RAISE EXCEPTION 'Invalid admin secret code'; END IF;
    IF EXISTS (SELECT 1 FROM app_users WHERE username=p_username) THEN RAISE EXCEPTION 'Username "%" is already taken', p_username; END IF;
    INSERT INTO app_users(username, password, role) VALUES(p_username, p_password, p_role);
END;
$BODY$;

-- SECURITY DEFINER so ufc_admin can create PG users without superuser
CREATE OR REPLACE PROCEDURE sp_create_user(p_username VARCHAR, p_password VARCHAR, p_role VARCHAR)
LANGUAGE plpgsql SECURITY DEFINER AS $BODY$
DECLARE v_grant_role VARCHAR;
BEGIN
    IF p_role NOT IN ('admin','guest') THEN RAISE EXCEPTION 'Role must be admin or guest'; END IF;
    IF p_username ~ '^\d+$' OR length(trim(p_username)) < 3 THEN RAISE EXCEPTION 'Username must be at least 3 chars and not numeric-only'; END IF;
    IF length(p_password) < 4 THEN RAISE EXCEPTION 'Password must be at least 4 characters'; END IF;
    IF EXISTS (SELECT 1 FROM pg_roles WHERE rolname=p_username) THEN RAISE EXCEPTION 'User % already exists', p_username; END IF;
    v_grant_role := CASE p_role WHEN 'admin' THEN 'ufc_admin' ELSE 'ufc_guest' END;
    EXECUTE format('CREATE USER %I WITH PASSWORD %L', p_username, p_password);
    EXECUTE format('GRANT %I TO %I', v_grant_role, p_username);
END;
$BODY$;

-- GRANTS
GRANT EXECUTE ON PROCEDURE sp_add_fight(VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR) TO ufc_admin;
GRANT EXECUTE ON PROCEDURE sp_update_fight(INT,VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR,VARCHAR) TO ufc_admin;
GRANT EXECUTE ON PROCEDURE sp_delete_by_id(INT) TO ufc_admin;
GRANT EXECUTE ON PROCEDURE sp_delete_by_event(VARCHAR) TO ufc_admin;
GRANT EXECUTE ON PROCEDURE sp_clear_table() TO ufc_admin;
GRANT EXECUTE ON PROCEDURE sp_create_user(VARCHAR,VARCHAR,VARCHAR) TO ufc_admin;
GRANT EXECUTE ON PROCEDURE sp_app_register(VARCHAR,VARCHAR,VARCHAR,VARCHAR) TO ufc_admin, ufc_guest;
GRANT EXECUTE ON FUNCTION fn_app_login(VARCHAR,VARCHAR) TO ufc_admin, ufc_guest;
GRANT EXECUTE ON FUNCTION fn_search_by_event(VARCHAR) TO ufc_admin, ufc_guest;
GRANT EXECUTE ON FUNCTION fn_get_all() TO ufc_admin, ufc_guest;
GRANT EXECUTE ON FUNCTION fn_get_by_id(INT) TO ufc_admin, ufc_guest;
