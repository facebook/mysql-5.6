
	query:
		insert_replace | insert_replace | insert_replace | insert_replace | insert_replace | insert_replace |
		update | update | update | update | update |
		delete |
		select | select | select ;

	_int: _mediumint_unsigned | _tinyint_unsigned | _digit ;
	_string: _english | _varchar(1) | _varchar(2) | _varchar(255) | REPEAT ( _varchar(2) , _tinyint_unsigned );
	_string_16: _english | _varchar(1) | _varchar(2) | _varchar(16) |  REPEAT ( _varchar(2) , _digit );
	_string_40: _english | _varchar(1) | _varchar(2) | _varchar(40) |  REPEAT ( _varchar(2) , _digit );
	_string_255: _english | _varchar(1) | _varchar(2) | _varchar(255) | REPEAT ( _varchar(2) , _tinyint_unsigned );
	_string_65534: _english | _varchar(1) | _varchar(2) | _varchar(255) | REPEAT ( _varchar(2) , _smallint_unsigned );
	_array_int: _int | _array_int , _int ;
	_raw: _digit ;
	_step_low: _digit | _tinyint_unsigned ;
	_step_high: _tinyint_unsigned | _smallint_unsigned ;
	_array_string: _string | _string , _string ;


select:
SELECT `data`, filetype
		FROM smf_admin_info_files
		WHERE filename = _string
		LIMIT 1|
	SELECT VERSION()|
	SELECT column_name, column_default, is_nullable, `data_type`, character_maximum_length
		FROM information_schema.columns
		WHERE table_name = _string
		ORDER BY ordinal_position|
	SELECT id_topic
					FROM smf_topics
					WHERE id_board = _int|
	SELECT COUNT(id_member) AS my_unapproved_posts
			FROM smf_messages
			WHERE id_topic = _int
				AND id_member = _int
				AND approved = 0|
	SELECT poster_time
		FROM smf_messages
		WHERE id_msg = _int
		LIMIT 1|
	SELECT IFNULL(lt.id_msg, IFNULL(lmr.id_msg, -1)) + 1 AS new_from
					FROM smf_topics AS t
						LEFT JOIN smf_log_topics AS lt ON (lt.id_topic = _int AND lt.id_member = _int)
						LEFT JOIN smf_log_mark_read AS lmr ON (lmr.id_board = _int AND lmr.id_member = _int)
					WHERE t.id_topic = _int
					LIMIT 1|
	SELECT COUNT(*)
					FROM smf_messages
					WHERE poster_time < _int
						AND id_topic = _int|
	SELECT COUNT(*)
					FROM smf_messages
					WHERE id_msg < _int
						AND id_topic = _int|
	SELECT
				lo.id_member, lo.log_time, mem.real_name, mem.member_name, mem.show_online,
				mg.online_color, mg.id_group, mg.group_name
			FROM smf_log_online AS lo
				LEFT JOIN smf_members AS mem ON (mem.id_member = lo.id_member)
				LEFT JOIN smf_membergroups AS mg ON (mg.id_group = CASE WHEN mem.id_group = _int THEN mem.id_post_group ELSE mem.id_group END)
			WHERE INSTR(lo.url, _string) > 0 OR lo.session = _string|
	SELECT cal.id_event, cal.start_date, cal.end_date, cal.title, cal.id_member, mem.real_name
			FROM smf_calendar AS cal
				LEFT JOIN smf_members AS mem ON (mem.id_member = cal.id_member)
			WHERE cal.id_topic = _int
			ORDER BY start_date|
	SELECT
				p.question, p.voting_locked, p.hide_results, p.expire_time, p.max_votes, p.change_vote,
				p.guest_vote, p.id_member, IFNULL(mem.real_name, p.poster_name) AS poster_name, p.num_guest_voters, p.reset_poll
			FROM smf_polls AS p
				LEFT JOIN smf_members AS mem ON (mem.id_member = p.id_member)
			WHERE p.id_poll = _int
			LIMIT 1|
	SELECT COUNT(DISTINCT id_member) AS total
			FROM smf_log_polls
			WHERE id_poll = _int
				AND id_member != _int|
	SELECT pc.id_choice, pc.label, pc.votes, IFNULL(lp.id_choice, -1) AS voted_this
			FROM smf_poll_choices AS pc
				LEFT JOIN smf_log_polls AS lp ON (lp.id_choice = pc.id_choice AND lp.id_poll = _int AND lp.id_member = _int AND lp.id_member != _int)
			WHERE pc.id_poll = _int|
	SELECT id_msg, id_member, approved
		FROM smf_messages
		WHERE id_topic = _int|
	SELECT sent, id_topic
			FROM smf_log_notify
			WHERE (id_topic = _int OR id_board = _int)
				AND id_member = _int
			LIMIT 2|
	SELECT COUNT(*)
				FROM smf_topics AS t
					LEFT JOIN smf_log_boards AS lb ON (lb.id_board = _int AND lb.id_member = _int)
					LEFT JOIN smf_log_topics AS lt ON (lt.id_topic = t.id_topic AND lt.id_member = _int)
				WHERE t.id_board = _int
					AND t.id_last_msg > IFNULL(lb.id_msg, 0)
					AND t.id_last_msg > IFNULL(lt.id_msg, 0)|
	SELECT
				id_msg, icon, subject, poster_time, poster_ip, id_member, modified_time, modified_name, body,
				smileys_enabled, poster_name, poster_email, approved,
				id_msg_modified < _int AS is_read
			FROM smf_messages
			WHERE id_msg IN (_array_int)
			ORDER BY id_msg|
	SELECT id_folder, filename, file_hash, fileext, id_attach, attachment_type, mime_type, approved
			FROM smf_attachments
			WHERE id_attach = _int
				AND id_member > _int
			LIMIT 1|
	SELECT a.id_folder, a.filename, a.file_hash, a.fileext, a.id_attach, a.attachment_type, a.mime_type, a.approved
			FROM smf_attachments AS a
				INNER JOIN smf_messages AS m ON (m.id_msg = a.id_msg AND m.id_topic = _int)
				INNER JOIN smf_boards AS b ON (b.id_board = m.id_board AND {query_see_board})
			WHERE a.id_attach = _int
			LIMIT 1|
	SELECT id_member_started
			FROM smf_topics
			WHERE id_topic = _int
			LIMIT 1|
	SELECT id_msg, subject, id_member, poster_time
		FROM smf_messages
		WHERE id_msg IN (_array_int)
			AND id_topic = _int|
	SELECT id_first_msg, id_last_msg
		FROM smf_topics
		WHERE id_topic = _int
		LIMIT 1|
	SELECT mg.id_group, mg.group_name, mg.description, mg.group_type, mg.online_color, mg.hidden,
			mg.stars, IFNULL(gm.id_member, 0) AS can_moderate
		FROM smf_membergroups AS mg
			LEFT JOIN smf_group_moderators AS gm ON (gm.id_group = mg.id_group AND gm.id_member = _int)
		WHERE mg.min_posts = _int
			AND mg.id_group != _int
		ORDER BY group_name|
	SELECT id_group, COUNT(*) AS num_members
			FROM smf_members
			WHERE id_group IN (_array_int)
			GROUP BY id_group|
	SELECT mg.id_group, COUNT(*) AS num_members
				FROM smf_membergroups AS mg
					INNER JOIN smf_members AS mem ON (mem.additional_groups != _string
						AND mem.id_group != mg.id_group
						AND FIND_IN_SET(mg.id_group, mem.additional_groups) != 0)
				WHERE mg.id_group IN (_array_int)
				GROUP BY mg.id_group|
	SELECT mg.id_group, mg.group_name, mg.description, mg.group_type, mg.online_color, mg.hidden,
			mg.stars, IFNULL(gm.id_member, 0) AS can_moderate
		FROM smf_membergroups AS mg
			LEFT JOIN smf_group_moderators AS gm ON (gm.id_group = mg.id_group AND gm.id_member = _int)
		WHERE mg.min_posts = _int
			AND mg.id_group != _int
		ORDER BY group_name|
	SELECT id_group, COUNT(*) AS num_members
			FROM smf_members
			WHERE id_group IN (_array_int)
			GROUP BY id_group|
	SELECT mg.id_group, COUNT(*) AS num_members
				FROM smf_membergroups AS mg
					INNER JOIN smf_members AS mem ON (mem.additional_groups != _string
						AND mem.id_group != mg.id_group
						AND FIND_IN_SET(mg.id_group, mem.additional_groups) != 0)
				WHERE mg.id_group IN (_array_int)
				GROUP BY mg.id_group|
	SELECT mods.id_group, mods.id_member, mem.member_name, mem.real_name
			FROM smf_group_moderators AS mods
				INNER JOIN smf_members AS mem ON (mem.id_member = mods.id_member)
			WHERE mods.id_group IN (_array_int)|
	SELECT id_group AS id, group_name AS name, CASE WHEN min_posts = _int THEN 1 ELSE 0 END AS assignable, hidden, online_color,
			stars, description, CASE WHEN min_posts != _int THEN 1 ELSE 0 END AS is_post_group
		FROM smf_membergroups
		WHERE id_group = _int
		LIMIT 1|
	SELECT mem.id_member, mem.real_name
		FROM smf_group_moderators AS mods
			INNER JOIN smf_members AS mem ON (mem.id_member = mods.id_member)
		WHERE mods.id_group = _int|
	SELECT code, filename, description
			FROM smf_smileys|
	SELECT action
			FROM smf_log_karma
			WHERE id_target = _int
				AND id_executor = _int
			LIMIT 1|
	SELECT variable, value
			FROM smf_settings|
	SELECT mem.*, IFNULL(a.id_attach, 0) AS id_attach, a.filename, a.attachment_type
				FROM smf_members AS mem
					LEFT JOIN smf_attachments AS a ON (a.id_member = _int)
				WHERE mem.id_member = _int
				LIMIT 1|
	SELECT poster_time
				FROM smf_messages
				WHERE id_msg = _int
				LIMIT 1|
	SELECT id_topic
				FROM smf_messages
				WHERE id_msg = _int
				LIMIT 1|
	SELECT COUNT(id_topic)
					FROM smf_topics
					WHERE id_member_started=_int
						AND approved = _int
						AND id_board = _int|
	SELECT permission, add_deny
			FROM smf_permissions
			WHERE id_group IN (_array_int)
				|
	SELECT *
			FROM smf_themes
			WHERE id_member|
	SELECT group_name AS member_group, online_color AS member_group_color, stars
				FROM smf_membergroups
				WHERE id_group = _int
				LIMIT 1|
	SELECT variable, value, id_member, id_theme
			FROM smf_themes
			WHERE id_member|
	SELECT
				b.id_parent, b.name, _int AS id_board, IFNULL(mem.id_member, 0) AS id_moderator,
				mem.real_name, b.child_level
			FROM smf_boards AS b
				LEFT JOIN smf_moderators AS mods ON (mods.id_board = b.id_board)
				LEFT JOIN smf_members AS mem ON (mem.id_member = mods.id_member)
			WHERE b.id_board = _int|
	SELECT `data`
		FROM smf_sessions
		WHERE session_id = _string
		LIMIT 1|
	SELECT id_member_started, locked
		FROM smf_topics
		WHERE id_topic = _int
		LIMIT 1|
	SELECT is_sticky
		FROM smf_topics
		WHERE id_topic = _int
		LIMIT 1|
	SELECT passwd, id_member, id_group, lngfile, is_activated, email_address, additional_groups, member_name, password_salt, openid_uri,
			passwd_flood
			FROM smf_members
			WHERE email_address = _string
			LIMIT 1|
	SELECT last_login
		FROM smf_members
		WHERE id_member = _int
			AND last_login = 0|
	SELECT
				_string AS id_msg, IFNULL(mem.real_name, _string) AS poster_name,
				mem.last_login AS poster_time, 0 AS id_topic, a.id_member, a.id_attach, a.filename, a.file_hash, a.attachment_type,
				a.size, a.width, a.height, a.downloads, _string AS subject, 0 AS id_board
			FROM smf_attachments AS a
				LEFT JOIN smf_members AS mem ON (mem.id_member = a.id_member)
			WHERE a.id_member != _int
			ORDER BY _raw
			LIMIT _int, _int|
	SELECT
				m.id_msg, IFNULL(mem.real_name, m.poster_name) AS poster_name, m.poster_time, m.id_topic, m.id_member,
				a.id_attach, a.filename, a.file_hash, a.attachment_type, a.size, a.width, a.height, a.downloads, mf.subject, t.id_board
			FROM smf_attachments AS a
				INNER JOIN smf_messages AS m ON (m.id_msg = a.id_msg)
				INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
				INNER JOIN smf_messages AS mf ON (mf.id_msg = t.id_first_msg)
				LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
			WHERE a.attachment_type = _int
			ORDER BY _raw
			LIMIT _int, _int|
	SELECT COUNT(*)
		FROM smf_attachments
		WHERE id_member != _int|
	SELECT COUNT(*) AS num_attach
			FROM smf_attachments AS a
				INNER JOIN smf_messages AS m ON (m.id_msg = a.id_msg)
				INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
				INNER JOIN smf_messages AS mf ON (mf.id_msg = t.id_first_msg)
			WHERE a.attachment_type = _int
				AND a.id_member = _int|
	SELECT COUNT(*)
		FROM smf_attachments
		WHERE attachment_type = _int
			AND id_member = _int|
	SELECT COUNT(*)
		FROM smf_attachments
		WHERE id_member != _int|
	SELECT id_attach, id_folder, id_member, filename, file_hash
		FROM smf_attachments
		WHERE attachment_type = _int
			AND id_member > _int|
	SELECT MAX(id_attach)
			FROM smf_attachments
			WHERE attachment_type = _int|
	SELECT thumb.id_attach, thumb.id_folder, thumb.filename, thumb.file_hash
				FROM smf_attachments AS thumb
					LEFT JOIN smf_attachments AS tparent ON (tparent.id_thumb = thumb.id_attach)
				WHERE thumb.id_attach BETWEEN _int AND _int + 499
					AND thumb.attachment_type = _int
					AND tparent.id_attach IS NULL|
	SELECT MAX(id_attach)
			FROM smf_attachments
			WHERE id_thumb != _int|
	SELECT a.id_attach
				FROM smf_attachments AS a
					LEFT JOIN smf_attachments AS thumb ON (thumb.id_attach = a.id_thumb)
				WHERE a.id_attach BETWEEN _int AND _int + 499
					AND a.id_thumb != _int
					AND thumb.id_attach IS NULL|
	SELECT MAX(id_attach)
			FROM smf_attachments|
	SELECT id_attach, id_folder, filename, file_hash, size, attachment_type
				FROM smf_attachments
				WHERE id_attach BETWEEN _int AND _int + 249|
	SELECT MAX(id_attach)
			FROM smf_attachments|
	SELECT a.id_attach, a.id_folder, a.filename, a.file_hash, a.attachment_type
				FROM smf_attachments AS a
					LEFT JOIN smf_members AS mem ON (mem.id_member = a.id_member)
				WHERE a.id_attach BETWEEN _int AND _int + 499
					AND a.id_member != _int
					AND a.id_msg = _int
					AND mem.id_member IS NULL|
	SELECT MAX(id_attach)
			FROM smf_attachments|
	SELECT a.id_attach, a.id_folder, a.filename, a.file_hash
				FROM smf_attachments AS a
					LEFT JOIN smf_messages AS m ON (m.id_msg = a.id_msg)
				WHERE a.id_attach BETWEEN _int AND _int + 499
					AND a.id_member = _int
					AND a.id_msg != _int
					AND m.id_msg IS NULL|
	SELECT id_attach
			FROM smf_attachments
			WHERE id_msg = _int
				AND approved = _int
				AND attachment_type = _int|
	SELECT a.id_attach, m.id_board, m.id_msg, m.id_topic
		FROM smf_attachments AS a
			INNER JOIN smf_messages AS m ON (m.id_msg = a.id_msg)
		WHERE a.id_attach IN (_array_int)
			AND a.attachment_type = _int
			AND a.approved = _int|
	SELECT
			a.id_attach, a.id_member, IFNULL(thumb.id_attach, 0) AS id_thumb
		FROM smf_attachments AS a
			LEFT JOIN smf_attachments AS thumb ON (thumb.id_attach = a.id_thumb)
		WHERE a.id_attach IN (_array_int)
			AND a.attachment_type = _int|
	SELECT COUNT(id_attach) AS num_attach
					FROM smf_attachments
					WHERE id_folder = _int|
	SELECT id_folder, COUNT(id_attach) AS num_attach
		FROM smf_attachments
		GROUP BY id_folder|
	SELECT bg.id_ban_group, bg.name, bg.ban_time, bg.expire_time, bg.reason, bg.notes, COUNT(bi.id_ban) AS num_triggers
		FROM smf_ban_groups AS bg
			LEFT JOIN smf_ban_items AS bi ON (bi.id_ban_group = bg.id_ban_group)
		GROUP BY bg.id_ban_group, bg.name, bg.ban_time, bg.expire_time, bg.reason, bg.notes
		ORDER BY _raw
		LIMIT _int, _int|
	SELECT COUNT(*) AS num_bans
		FROM smf_ban_groups|
	SELECT id_member
				FROM smf_members
				WHERE (id_group = _int OR FIND_IN_SET(_int, additional_groups) != 0)
					AND email_address LIKE _string
				LIMIT 1|
	SELECT id_member, (id_group = _int OR FIND_IN_SET(_int, additional_groups) != 0) AS isAdmin
				FROM smf_members
				WHERE member_name = _string OR real_name = _string
				LIMIT 1|
	SELECT id_ban_group
			FROM smf_ban_groups
			WHERE name = _string|
	SELECT id_member, (id_group = _int OR FIND_IN_SET(_int, additional_groups) != 0) AS isAdmin
							FROM smf_members
							WHERE member_name = _string OR real_name = _string
							LIMIT 1|
	SELECT
				bi.id_ban, bi.hostname, bi.email_address, bi.id_member, bi.hits,
				bi.ip_low1, bi.ip_high1, bi.ip_low2, bi.ip_high2, bi.ip_low3, bi.ip_high3, bi.ip_low4, bi.ip_high4,
				bg.id_ban_group, bg.name, bg.ban_time, bg.expire_time, bg.reason, bg.notes, bg.cannot_access, bg.cannot_register, bg.cannot_login, bg.cannot_post,
				IFNULL(mem.id_member, 0) AS id_member, mem.member_name, mem.real_name
			FROM smf_ban_groups AS bg
				LEFT JOIN smf_ban_items AS bi ON (bi.id_ban_group = bg.id_ban_group)
				LEFT JOIN smf_members AS mem ON (mem.id_member = bi.id_member)
			WHERE bg.id_ban_group = _int|
	SELECT id_member, real_name, member_ip, email_address
				FROM smf_members
				WHERE id_member = _int
				LIMIT 1|
	SELECT DISTINCT poster_ip
					FROM smf_messages
					WHERE id_member = _int
						AND poster_ip RLIKE _string
					ORDER BY poster_ip|
	SELECT DISTINCT ip
					FROM smf_log_errors
					WHERE id_member = _int
						AND ip RLIKE _string
					ORDER BY ip|
	SELECT
				bi.id_ban, bi.id_ban_group, bi.hostname, bi.email_address, bi.id_member,
				bi.ip_low1, bi.ip_high1, bi.ip_low2, bi.ip_high2, bi.ip_low3, bi.ip_high3, bi.ip_low4, bi.ip_high4,
				mem.member_name, mem.real_name
			FROM smf_ban_items AS bi
				LEFT JOIN smf_members AS mem ON (mem.id_member = bi.id_member)
			WHERE bi.id_ban = _int
				AND bi.id_ban_group = _int
			LIMIT 1|
	SELECT COUNT(*)
		FROM smf_ban_items AS bi|
	SELECT COUNT(*)
		FROM smf_log_banned AS lb|
	SELECT bi.id_member, bi.email_address
		FROM smf_ban_items AS bi
			INNER JOIN smf_ban_groups AS bg ON (bg.id_ban_group = bi.id_ban_group)
		WHERE (bi.id_member > _int OR bi.email_address != _string)
			AND bg.cannot_access = _int
			AND (bg.expire_time IS NULL OR bg.expire_time > _int)|
	SELECT mem.id_member, mem.is_activated - 10 AS new_value
		FROM smf_members AS mem
			LEFT JOIN smf_ban_items AS bi ON (bi.id_member = mem.id_member OR mem.email_address LIKE bi.email_address)
			LEFT JOIN smf_ban_groups AS bg ON (bg.id_ban_group = bi.id_ban_group AND bg.cannot_access = _int AND (bg.expire_time IS NULL OR bg.expire_time > _int))
		WHERE (bi.id_ban IS NULL OR bg.id_ban_group IS NULL)
			AND mem.is_activated >= _int|
	SELECT group_name, id_group, min_posts
		FROM smf_membergroups
		WHERE id_group > _int OR id_group = _int
		ORDER BY min_posts, id_group != _int, group_name|
	SELECT mem.id_member, mem.real_name
		FROM smf_moderators AS mods
			INNER JOIN smf_members AS mem ON (mem.id_member = mods.id_member)
		WHERE mods.id_board = _int|
	SELECT id_theme AS id, value AS name
		FROM smf_themes
		WHERE variable = _string|
	SELECT redirect, num_posts
				FROM smf_boards
				WHERE id_board = _int|
	SELECT CONCAT(_string, _string, _string)
		FROM smf_categories
		LIMIT 1|
	SELECT b.id_board, b.name AS board_name, c.name AS cat_name
		FROM smf_boards AS b
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
		WHERE redirect = _string|
	SELECT id_holiday, YEAR(event_date) AS year, MONTH(event_date) AS month, DAYOFMONTH(event_date) AS day, title
			FROM smf_calendar_holidays
			WHERE id_holiday = _int
			LIMIT 1|
	SELECT b.id_board, b.name AS board_name, c.name AS cat_name
		FROM smf_boards AS b
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)|
	SELECT COUNT(*)
		FROM smf_log_errors|
	SELECT id_error, id_member, ip, url, log_time, message, session, error_type, file, line
		FROM smf_log_errors|
	SELECT error_type, COUNT(*) AS num_errors
		FROM smf_log_errors
		GROUP BY error_type
		ORDER BY error_type = _string DESC, error_type ASC|
	SELECT COUNT(*) AS queue_size, MIN(time_sent) AS oldest
		FROM smf_mail_queue|
	SELECT
			id_mail, time_sent, recipient, priority, private, subject
		FROM smf_mail_queue
		ORDER BY _raw
		LIMIT _int, _int|
	SELECT COUNT(*) AS queue_size
		FROM smf_mail_queue|
	SELECT COUNT(*) AS queue_size
			FROM smf_mail_queue|
	SELECT id_group, group_name
		FROM smf_membergroups|
	SELECT b.id_board, b.name, b.child_level, c.name AS cat_name, c.id_cat
		FROM smf_boards AS b
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
		WHERE {query_see_board}
			AND redirect = _string|
	SELECT MAX(id_topic)
		FROM smf_topics|
	SELECT /*!40001 SQL_NO_CACHE */ t.id_topic, MAX(t.num_replies) AS num_replies,
					CASE WHEN COUNT(ma.id_msg) >= 1 THEN COUNT(ma.id_msg) - 1 ELSE 0 END AS real_num_replies
				FROM smf_topics AS t
					LEFT JOIN smf_messages AS ma ON (ma.id_topic = t.id_topic AND ma.approved = _int)
				WHERE t.id_topic > _int
					AND t.id_topic <= _int
				GROUP BY t.id_topic
				HAVING CASE WHEN COUNT(ma.id_msg) >= 1 THEN COUNT(ma.id_msg) - 1 ELSE 0 END != MAX(t.num_replies)|
	SELECT /*!40001 SQL_NO_CACHE */ t.id_topic, MAX(t.unapproved_posts) AS unapproved_posts,
					COUNT(mu.id_msg) AS real_unapproved_posts
				FROM smf_topics AS t
					LEFT JOIN smf_messages AS mu ON (mu.id_topic = t.id_topic AND mu.approved = _int)
				WHERE t.id_topic > _int
					AND t.id_topic <= _int
				GROUP BY t.id_topic
				HAVING COUNT(mu.id_msg) != MAX(t.unapproved_posts)|
	SELECT /*!40001 SQL_NO_CACHE */ m.id_board, COUNT(*) AS real_num_posts
				FROM smf_messages AS m
				WHERE m.id_topic > _int
					AND m.id_topic <= _int
					AND m.approved = _int
				GROUP BY m.id_board|
	SELECT /*!40001 SQL_NO_CACHE */ t.id_board, COUNT(*) AS real_num_topics
				FROM smf_topics AS t
				WHERE t.approved = _int
					AND t.id_topic > _int
					AND t.id_topic <= _int
				GROUP BY t.id_board|
	SELECT /*!40001 SQL_NO_CACHE */ m.id_board, COUNT(*) AS real_unapproved_posts
				FROM smf_messages AS m
				WHERE m.id_topic > _int
					AND m.id_topic <= _int
					AND m.approved = _int
				GROUP BY m.id_board|
	SELECT /*!40001 SQL_NO_CACHE */ t.id_board, COUNT(*) AS real_unapproved_topics
				FROM smf_topics AS t
				WHERE t.approved = _int
					AND t.id_topic > _int
					AND t.id_topic <= _int
				GROUP BY t.id_board|
	SELECT /*!40001 SQL_NO_CACHE */ mem.id_member, COUNT(pmr.id_pm) AS real_num,
				MAX(mem.instant_messages) AS instant_messages
			FROM smf_members AS mem
				LEFT JOIN smf_pm_recipients AS pmr ON (mem.id_member = pmr.id_member AND pmr.deleted = _int)
			GROUP BY mem.id_member
			HAVING COUNT(pmr.id_pm) != MAX(mem.instant_messages)|
	SELECT /*!40001 SQL_NO_CACHE */ mem.id_member, COUNT(pmr.id_pm) AS real_num,
				MAX(mem.unread_messages) AS unread_messages
			FROM smf_members AS mem
				LEFT JOIN smf_pm_recipients AS pmr ON (mem.id_member = pmr.id_member AND pmr.deleted = _int AND pmr.is_read = _int)
			GROUP BY mem.id_member
			HAVING COUNT(pmr.id_pm) != MAX(mem.unread_messages)|
	SELECT /*!40001 SQL_NO_CACHE */ t.id_board, m.id_msg
				FROM smf_messages AS m
					INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic AND t.id_board != m.id_board)
				WHERE m.id_msg > _int
					AND m.id_msg <= _int|
	SELECT m.id_board, MAX(m.id_msg) AS local_last_msg
		FROM smf_messages AS m
		WHERE m.approved = _int
		GROUP BY m.id_board|
	SELECT /*!40001 SQL_NO_CACHE */ id_board, id_parent, id_last_msg, child_level, id_msg_updated
		FROM smf_boards|
	SELECT id_group, group_name, min_posts
			FROM smf_membergroups|
	SELECT COUNT(*)
			FROM smf_topics
			WHERE id_board = _int|
	SELECT id_topic
				FROM smf_topics
				WHERE id_board = _int
				LIMIT 10|
	SELECT MAX(id_group)
			FROM smf_membergroups|
	SELECT permission, add_deny
				FROM smf_permissions
				WHERE id_group = _int|
	SELECT id_profile, permission, add_deny
				FROM smf_board_permissions
				WHERE id_group = _int|
	SELECT online_color, max_messages, stars
					FROM smf_membergroups
					WHERE id_group = _int
					LIMIT 1|
	SELECT id_group, group_name
		FROM smf_membergroups
		WHERE (id_group > _int OR id_group = _int)|
	SELECT id_board, name, child_level
		FROM smf_boards|
	SELECT id_board, member_groups
				FROM smf_boards
				WHERE FIND_IN_SET(_string, member_groups) != 0|
	SELECT id_member, additional_groups
				FROM smf_members
				WHERE FIND_IN_SET(_string, additional_groups) != 0|
	SELECT id_member, additional_groups
					FROM smf_members
					WHERE id_group = _int
						AND FIND_IN_SET(_int, additional_groups) = 0|
	SELECT COUNT(*)
				FROM smf_membergroups
				WHERE group_type != _int|
	SELECT id_member
						FROM smf_members
						WHERE id_member IN (_array_int)
						LIMIT _int|
	SELECT group_name, description, min_posts, online_color, max_messages, stars, group_type, hidden, id_parent
		FROM smf_membergroups
		WHERE id_group = _int
		LIMIT 1|
	SELECT mem.id_member, mem.real_name
		FROM smf_group_moderators AS mods
			INNER JOIN smf_members AS mem ON (mem.id_member = mods.id_member)
		WHERE mods.id_group = _int|
	SELECT id_board, name, child_level, FIND_IN_SET(_string, member_groups) != 0 AS can_access
			FROM smf_boards|
	SELECT id_group, group_name
		FROM smf_membergroups
		WHERE id_group != _int|
	SELECT COUNT(*) AS total_members, is_activated
		FROM smf_members
		WHERE is_activated != _int
		GROUP BY is_activated|
	SELECT id_group, group_name, min_posts
			FROM smf_membergroups
			WHERE id_group != _int
			ORDER BY min_posts, CASE WHEN id_group < _int THEN id_group ELSE 4 END, group_name|
	SELECT id_group, group_name, min_posts
		FROM smf_membergroups
		WHERE id_group != _int
		ORDER BY min_posts, CASE WHEN id_group < _int THEN id_group ELSE 4 END, group_name|
	SELECT id_member, member_name, real_name, email_address, validation_code, lngfile
		FROM smf_members
		WHERE is_activated = _int|
	SELECT mg.id_group, mg.group_name, mg.min_posts
		FROM smf_membergroups AS mg|
	SELECT mem.id_post_group AS id_group, COUNT(*) AS member_count
			FROM smf_members AS mem
			WHERE mem.id_post_group IN (_array_int)
			GROUP BY mem.id_post_group|
	SELECT id_group, COUNT(*) AS member_count
			FROM smf_members
			WHERE id_group IN (_array_int)
			GROUP BY id_group|
	SELECT mg.id_group, COUNT(*) AS member_count
			FROM smf_membergroups AS mg
				INNER JOIN smf_members AS mem ON (mem.additional_groups != _string
					AND mem.id_group != mg.id_group
					AND FIND_IN_SET(mg.id_group, mem.additional_groups) != 0)
			WHERE mg.id_group IN (_array_int)
			GROUP BY mg.id_group|
	SELECT COUNT(DISTINCT id_member) AS num_distinct_mods
		FROM smf_moderators
		LIMIT 1|
	SELECT DISTINCT mem.id_member
		FROM smf_ban_groups AS bg
			INNER JOIN smf_ban_items AS bi ON (bg.id_ban_group = bi.id_ban_group)
			INNER JOIN smf_members AS mem ON (bi.id_member = mem.id_member)
		WHERE (bg.cannot_access = _int OR bg.cannot_login = _int)
			AND (bg.expire_time IS NULL OR bg.expire_time > _int)|
	SELECT DISTINCT bi.email_address
		FROM smf_ban_items AS bi
			INNER JOIN smf_ban_groups AS bg ON (bg.id_ban_group = bi.id_ban_group)
		WHERE (bg.cannot_access = _int OR bg.cannot_login = _int)
			AND (COALESCE(bg.expire_time, 1=1) OR bg.expire_time > _int)
			AND bi.email_address != _string|
	SELECT DISTINCT mem.id_member AS identifier
			FROM smf_members AS mem
				INNER JOIN smf_moderators AS mods ON (mods.id_member = mem.id_member)
			WHERE mem.is_activated = _int|
	SELECT MAX(id_member)
		FROM smf_members|
	SELECT COUNT(*)
				FROM smf_log_subscribed
				WHERE id_subscribe = _int
					AND status = _int|
	SELECT name, description, cost, length, id_group, add_groups, active, repeatable, allow_partial, email_complete, reminder
			FROM smf_subscriptions
			WHERE id_subscribe = _int
			LIMIT 1|
	SELECT COUNT(*)
			FROM smf_log_subscribed
			WHERE id_subscribe = _int
				AND status = _int|
	SELECT id_group, group_name
		FROM smf_membergroups
		WHERE id_group != _int
			AND min_posts = _int|
	SELECT id_subscribe, name, description, cost, length, id_group, add_groups, active
		FROM smf_subscriptions
		WHERE id_subscribe = _int|
	SELECT COUNT(*) AS total_subs
		FROM smf_log_subscribed AS ls
			LEFT JOIN smf_members AS mem ON (mem.id_member = ls.id_member)
		WHERE ls.id_subscribe = _int |
	SELECT ls.id_sublog, IFNULL(mem.id_member, 0) AS id_member, IFNULL(mem.real_name, _string) AS name, ls.start_time, ls.end_time,
			ls.status, ls.payments_pending
		FROM smf_log_subscribed AS ls
			LEFT JOIN smf_members AS mem ON (mem.id_member = ls.id_member)
		WHERE ls.id_subscribe = _int |
	SELECT id_subscribe
			FROM smf_log_subscribed
			WHERE id_sublog = _int|
	SELECT id_member, id_group
				FROM smf_members
				WHERE real_name = _string
				LIMIT 1|
	SELECT id_subscribe
				FROM smf_log_subscribed
				WHERE id_subscribe = _int
					AND id_member = _int|
	SELECT id_member, status
				FROM smf_log_subscribed
				WHERE id_sublog = _int|
	SELECT id_subscribe, id_member
				FROM smf_log_subscribed
				WHERE id_sublog IN (_array_int)|
	SELECT real_name
				FROM smf_members
				WHERE id_member = _int|
	SELECT ls.id_sublog, ls.id_subscribe, ls.id_member, start_time, end_time, status, payments_pending, pending_details,
				IFNULL(mem.real_name, _string) AS username
			FROM smf_log_subscribed AS ls
				LEFT JOIN smf_members AS mem ON (mem.id_member = ls.id_member)
			WHERE ls.id_sublog = _int
			LIMIT 1|
	SELECT id_member, id_group, additional_groups
		FROM smf_members
		WHERE id_member IN (_array_int)|
	SELECT ls.id_member, ls.old_id_group, s.id_group, s.add_groups
		FROM smf_log_subscribed AS ls
			INNER JOIN smf_subscriptions AS s ON (s.id_subscribe = ls.id_subscribe)
		WHERE ls.id_member IN (_array_int)
			AND ls.end_time > _int|
	SELECT id_sublog, end_time, start_time
		FROM smf_log_subscribed
		WHERE id_subscribe = _int
			AND id_member = _int
			AND status = _int|
	SELECT m.id_group, m.additional_groups
		FROM smf_members AS m
		WHERE m.id_member = _int|
	SELECT id_sublog, end_time, start_time
		FROM smf_log_subscribed
		WHERE id_subscribe = _int
			AND id_member = _int|
	SELECT m.id_group, m.additional_groups
		FROM smf_members AS m
		WHERE m.id_member = _int|
	SELECT id_subscribe, old_id_group
		FROM smf_log_subscribed
		WHERE id_member = _int
			AND status = _int|
	SELECT id_subscribe, name, description, cost, length, id_group, add_groups, active, repeatable
		FROM smf_subscriptions|
	SELECT COUNT(id_sublog) AS member_count, id_subscribe, status
		FROM smf_log_subscribed
		GROUP BY id_subscribe, status|
	SELECT SUM(payments_pending) AS total_pending, id_subscribe
		FROM smf_log_subscribed
		GROUP BY id_subscribe|
	SELECT COUNT(*)
		FROM smf_members
		WHERE id_group = _int|
	SELECT id_group, id_parent, group_name, min_posts, online_color, stars
		FROM smf_membergroups|
	SELECT id_post_group AS id_group, COUNT(*) AS num_members
			FROM smf_members
			WHERE id_post_group IN (_array_int)
			GROUP BY id_post_group|
	SELECT id_group, COUNT(*) AS num_members
			FROM smf_members
			WHERE id_group IN (_array_int)
			GROUP BY id_group|
	SELECT mg.id_group, COUNT(*) AS num_members
			FROM smf_membergroups AS mg
				INNER JOIN smf_members AS mem ON (mem.additional_groups != _string
					AND mem.id_group != mg.id_group
					AND FIND_IN_SET(mg.id_group, mem.additional_groups) != 0)
			WHERE mg.id_group IN (_array_int)
			GROUP BY mg.id_group|
	SELECT id_group, COUNT(*) AS num_permissions, add_deny
			FROM smf_permissions
			|
	SELECT id_profile, id_group, COUNT(*) AS num_permissions, add_deny
			FROM smf_board_permissions
			WHERE id_profile = _int
			|
	SELECT id_profile, id_group, COUNT(*) AS num_permissions, add_deny
			FROM smf_board_permissions
			WHERE id_profile = _int
			GROUP BY id_profile, id_group, add_deny|
	SELECT permission, add_deny
				FROM smf_permissions
				WHERE id_group = _int|
	SELECT permission, add_deny
			FROM smf_board_permissions
			WHERE id_group = _int
				AND id_profile = _int|
	SELECT group_name, id_parent
			FROM smf_membergroups
			WHERE id_group = _int
			LIMIT 1|
	SELECT permission, add_deny
			FROM smf_permissions
			WHERE id_group = _int|
	SELECT permission, add_deny
		FROM smf_board_permissions
		WHERE id_group = _int
			AND id_profile = _int|
	SELECT id_parent
			FROM smf_membergroups
			WHERE id_group = _int
			LIMIT 1|
	SELECT id_group
				FROM smf_membergroups
				WHERE min_posts != _int|
	SELECT id_group
			FROM smf_membergroups
			WHERE id_group > _int
			ORDER BY min_posts, CASE WHEN id_group < _int THEN id_group ELSE 4 END, group_name|
	SELECT id_group, CASE WHEN add_deny = _int THEN _string ELSE _string END AS status, permission
		FROM smf_permissions
		WHERE id_group IN (-1, 0)
			AND permission IN (_array_string)|
	SELECT mg.id_group, mg.group_name, mg.min_posts, IFNULL(p.add_deny, -1) AS status, p.permission
		FROM smf_membergroups AS mg
			LEFT JOIN smf_permissions AS p ON (p.id_group = mg.id_group AND p.permission IN (_array_string))
		WHERE mg.id_group NOT IN (1, 3)
			AND mg.id_parent = _int|
	SELECT id_profile, profile_name
		FROM smf_permission_profiles
		ORDER BY id_profile|
	SELECT id_group, permission, add_deny
			FROM smf_board_permissions
			WHERE id_profile = _int|
	SELECT id_board
			FROM smf_boards
			WHERE id_profile IN (_array_int)
			LIMIT 1|
	SELECT id_profile, COUNT(id_board) AS board_count
		FROM smf_boards
		GROUP BY id_profile|
	SELECT id_parent, id_group
		FROM smf_membergroups
		WHERE id_parent != _int
			|
	SELECT id_group, permission, add_deny
			FROM smf_permissions
			WHERE id_group IN (_array_int)|
	SELECT id_profile, id_group, permission, add_deny
			FROM smf_board_permissions
			WHERE id_group IN (_array_int)
				|
	SELECT id_group, group_name, online_color, id_parent
		FROM smf_membergroups
		WHERE id_group != _int
			|
	SELECT id_group, permission, add_deny
		FROM smf_board_permissions
		WHERE id_profile = _int
			AND permission IN (_array_string)
			AND id_group IN (_array_int)|
	SELECT group_name, id_group
			FROM smf_membergroups
			WHERE id_group != _int
				AND min_posts = _int|
	SELECT id_task, `next_time`, `time_offset`, `time_regularity`, `time_unit`, disabled, task
		FROM smf_scheduled_tasks|
	SELECT id_task, `next_time`, `time_offset`, `time_regularity`, `time_unit`, disabled, task
		FROM smf_scheduled_tasks
		WHERE id_task = _int|
	SELECT COUNT(*)
		FROM smf_log_scheduled_tasks|
	SELECT id_group, group_name
		FROM smf_membergroups
		WHERE id_group != _int
			AND id_group != _int|
	SELECT id_spider, MAX(last_seen) AS last_seen_time
		FROM smf_log_spider_stats
		GROUP BY id_spider|
	SELECT COUNT(*) AS num_spiders
		FROM smf_spiders|
	SELECT id_spider, spider_name, user_agent, ip_info
			FROM smf_spiders
			WHERE id_spider = _int|
	SELECT id_spider, user_agent, ip_info
			FROM smf_spiders|
	SELECT id_spider, MAX(log_time) AS last_seen, COUNT(*) AS num_hits
		FROM smf_log_spider_hits
		WHERE processed = _int
		GROUP BY id_spider, MONTH(log_time), DAYOFMONTH(log_time)|
	SELECT COUNT(*) AS num_logs
		FROM smf_log_spider_hits|
	SELECT MIN(stat_date) AS first_date, MAX(stat_date) AS last_date
		FROM smf_log_spider_stats|
	SELECT COUNT(*) AS offset
			FROM smf_log_spider_stats
			WHERE stat_date < _date|
	SELECT COUNT(*) AS num_stats
		FROM smf_log_spider_stats|
	SELECT id_spider, spider_name
		FROM smf_spiders|
	SELECT id_msg >= _int AS todo, COUNT(*) AS num_messages
			FROM smf_messages
			GROUP BY todo|
	SELECT id_msg, body
					FROM smf_messages
					WHERE id_msg BETWEEN _int AND _int
					LIMIT _int|
	SELECT id_theme, value
				FROM smf_themes
				WHERE id_member = _int
					AND variable = _string
					AND id_theme IN (_array_int)|
	SELECT lngfile, COUNT(*) AS num_users
		FROM smf_members
		GROUP BY lngfile|
	SELECT id_theme, variable, value
		FROM smf_themes
		WHERE id_theme != _int
			AND id_member = _int
			AND variable IN (_string, _string)|
	SELECT id_comment, body AS question, recipient_name AS answer
		FROM smf_log_comments
		WHERE comment_type = _string|
	SELECT MAX(id_member)
			FROM smf_members|
	SELECT id_field, col_name, field_name, field_desc, field_type, active, placement
			FROM smf_custom_fields
			ORDER BY _raw
			LIMIT _int, _int|
	SELECT COUNT(*)
		FROM smf_custom_fields|
	SELECT
				id_field, col_name, field_name, field_desc, field_type, field_length, field_options,
				show_reg, show_display, show_profile, private, active, default_value, can_search,
				bbc, mask, enclose, placement
			FROM smf_custom_fields
			WHERE id_field = _int|
	SELECT id_field
					FROM smf_custom_fields
					WHERE col_name = _string|
	SELECT col_name, field_name, bbc, enclose, placement
			FROM smf_custom_fields
			WHERE show_display = _int
				AND active = _int
				AND private != _int
				AND private != _int|
	SELECT filename
					FROM smf_smileys
					WHERE filename IN (_array_string)|
	SELECT MAX(smiley_order) + 1
				FROM smf_smileys
				WHERE hidden = _int
					AND smiley_row = _int|
	SELECT id_smiley AS id, code, filename, description, hidden AS location, 0 AS is_new
			FROM smf_smileys
			WHERE id_smiley = _int|
	SELECT COUNT(*)
		FROM smf_smileys|
	SELECT smiley_row, smiley_order, hidden
				FROM smf_smileys
				WHERE hidden = _int
					AND id_smiley = _int|
	SELECT id_smiley, code, filename, description, smiley_row, smiley_order, hidden
		FROM smf_smileys
		WHERE hidden != _int
		ORDER BY smiley_order, smiley_row|
	SELECT filename
		FROM smf_smileys
		WHERE filename IN (_array_string)|
	SELECT MAX(smiley_order)
		FROM smf_smileys
		WHERE hidden = _int
			AND smiley_row = _int|
	SELECT m.id_icon, m.title, m.filename, m.icon_order, m.id_board, b.name AS board_name
		FROM smf_message_icons AS m
			LEFT JOIN smf_boards AS b ON (b.id_board = m.id_board)
		WHERE ({query_see_board} OR b.id_board IS NULL)|
	SELECT m.id_icon, m.title, m.filename, m.icon_order, m.id_board, b.name AS board_name
		FROM smf_message_icons AS m
			LEFT JOIN smf_boards AS b ON (b.id_board = m.id_board)
		WHERE ({query_see_board} OR b.id_board IS NULL)|
	SELECT real_name
				FROM smf_members
				WHERE is_activated = _int
				ORDER BY real_name|
	SELECT COUNT(*)
			FROM smf_members
			WHERE is_activated = _int|
	SELECT COUNT(*)
			FROM smf_members
			WHERE LOWER(SUBSTRING(real_name, 1, 1)) < _string
				AND is_activated = _int|
	SELECT mem.id_member
		FROM smf_members AS mem|
	SELECT col_name, field_name, field_desc
		FROM smf_custom_fields
		WHERE active = _int
			|
	SELECT COUNT(*)
			FROM smf_members AS mem
				LEFT JOIN smf_membergroups AS mg ON (mg.id_group = CASE WHEN mem.id_group = _int THEN mem.id_post_group ELSE mem.id_group END)|
	SELECT mem.id_member
			FROM smf_members AS mem
				LEFT JOIN smf_log_online AS lo ON (lo.id_member = mem.id_member)
				LEFT JOIN smf_membergroups AS mg ON (mg.id_group = CASE WHEN mem.id_group = _int THEN mem.id_post_group ELSE mem.id_group END)|
	SELECT MAX(posts)
		FROM smf_members|
	SELECT sent
			FROM smf_log_notify
			WHERE id_board = _int
				AND id_member = _int
			LIMIT 1|
	SELECT
				lo.id_member, lo.log_time, mem.real_name, mem.member_name, mem.show_online,
				mg.online_color, mg.id_group, mg.group_name
			FROM smf_log_online AS lo
				LEFT JOIN smf_members AS mem ON (mem.id_member = lo.id_member)
				LEFT JOIN smf_membergroups AS mg ON (mg.id_group = CASE WHEN mem.id_group = _int THEN mem.id_post_group ELSE mem.id_group END)
			WHERE INSTR(lo.url, _string) > 0 OR lo.session = _string|
	SELECT t.id_topic
			FROM smf_topics AS t|
	SELECT t.id_topic, t.id_board, b.count_posts
			FROM smf_topics AS t
				LEFT JOIN smf_boards AS b ON (t.id_board = b.id_board)
			WHERE t.id_topic IN (_array_int)|
	SELECT id_board, count_posts
				FROM smf_boards
				WHERE id_board IN (_array_int)|
	SELECT id_member, id_topic
					FROM smf_messages
					WHERE id_topic IN (_array_int)|
	SELECT id_topic, id_board
			FROM smf_topics
			WHERE id_topic IN (_array_int)|
	SELECT id_member, real_name, last_login
			FROM smf_members
			WHERE warning >= _int
			ORDER BY last_login DESC
			LIMIT 10|
	SELECT COUNT(*)
			FROM smf_log_comments AS lc
				LEFT JOIN smf_members AS mem ON (mem.id_member = lc.id_member)
			WHERE lc.comment_type = _string|
	SELECT IFNULL(mem.id_member, 0) AS id_member, IFNULL(mem.real_name, lc.member_name) AS member_name,
				lc.log_time, lc.body, lc.id_comment AS id_note
			FROM smf_log_comments AS lc
				LEFT JOIN smf_members AS mem ON (mem.id_member = lc.id_member)
			WHERE lc.comment_type = _string
			ORDER BY id_comment DESC
			LIMIT _int, 10|
	SELECT lrc.id_comment, lrc.id_report, lrc.`time_sent`, lrc.comment,
				IFNULL(mem.id_member, 0) AS id_member, IFNULL(mem.real_name, lrc.membername) AS reporter
			FROM smf_log_reported_comments AS lrc
				LEFT JOIN smf_members AS mem ON (mem.id_member = lrc.id_member)
			WHERE lrc.id_report IN (_array_int)|
	SELECT lrc.id_comment, lrc.id_report, lrc.`time_sent`, lrc.comment,
			IFNULL(mem.id_member, 0) AS id_member, IFNULL(mem.real_name, lrc.membername) AS reporter
		FROM smf_log_reported_comments AS lrc
			LEFT JOIN smf_members AS mem ON (mem.id_member = lrc.id_member)
		WHERE lrc.id_report = _int|
	SELECT lc.id_comment, lc.id_notice, lc.log_time, lc.body,
			IFNULL(mem.id_member, 0) AS id_member, IFNULL(mem.real_name, lc.member_name) AS moderator
		FROM smf_log_comments AS lc
			LEFT JOIN smf_members AS mem ON (mem.id_member = lc.id_member)
		WHERE lc.id_notice = _int
			AND lc.comment_type = _string|
	SELECT body, subject
		FROM smf_log_member_notices
		WHERE id_notice = _int|
	SELECT COUNT(*)
		FROM smf_members
		WHERE warning >= _int|
	SELECT m.id_member, MAX(m.id_msg) AS last_post_id
			FROM smf_messages AS m|
	SELECT id_member, poster_time
				FROM smf_messages
				WHERE id_msg IN (_array_int)|
	SELECT MAX(m.poster_time) AS last_post, MAX(m.id_msg) AS last_post_id, m.id_member
			FROM smf_messages AS m|
	SELECT COUNT(*)
			FROM smf_messages AS m
				INNER JOIN smf_members AS mem ON (mem.id_member = m.id_member)
				INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
			WHERE mem.warning >= _int
				AND {query_see_board}
				|
	SELECT m.id_msg, m.id_topic, m.id_board, m.id_member, m.subject, m.body, m.poster_time,
			m.approved, mem.real_name, m.smileys_enabled
		FROM smf_messages AS m
			INNER JOIN smf_members AS mem ON (mem.id_member = m.id_member)
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
		WHERE mem.warning >= _int
			AND {query_see_board}
			|
	SELECT COUNT(*)
		FROM smf_log_comments
		WHERE comment_type = _string|
	SELECT recipient_name
			FROM smf_log_comments
			WHERE id_comment IN (_array_int)
				AND comment_type = _string
				AND (id_recipient = _int OR id_recipient = _int)|
	SELECT COUNT(*)
		FROM smf_log_comments
		WHERE comment_type = _string
			AND (id_recipient = _string OR id_recipient = _int)|
	SELECT id_member, id_recipient, recipient_name AS template_title, body
			FROM smf_log_comments
			WHERE id_comment = _int
				AND comment_type = _string
				AND (id_recipient = _int OR id_recipient = _int)|
	SELECT COUNT(*)
		FROM smf_log_actions AS lm
			LEFT JOIN smf_members AS mem ON (mem.id_member = lm.id_member)
			LEFT JOIN smf_membergroups AS mg ON (mg.id_group = CASE WHEN mem.id_group = _int THEN mem.id_post_group ELSE mem.id_group END)
			LEFT JOIN smf_boards AS b ON (b.id_board = lm.id_board)
			LEFT JOIN smf_topics AS t ON (t.id_topic = lm.id_topic)
		WHERE id_log = _int
			AND _raw|
	SELECT
			lm.id_action, lm.id_member, lm.ip, lm.log_time, lm.action, lm.id_board, lm.id_topic, lm.id_msg, lm.extra,
			mem.real_name, mg.group_name
		FROM smf_log_actions AS lm
			LEFT JOIN smf_members AS mem ON (mem.id_member = lm.id_member)
			LEFT JOIN smf_membergroups AS mg ON (mg.id_group = CASE WHEN mem.id_group = _int THEN mem.id_post_group ELSE mem.id_group END)
			LEFT JOIN smf_boards AS b ON (b.id_board = lm.id_board)
			LEFT JOIN smf_topics AS t ON (t.id_topic = lm.id_topic)
			WHERE id_log = _int
				AND _raw|
	SELECT t.id_member_started, ms.subject, t.approved
		FROM smf_topics AS t
			INNER JOIN smf_messages AS ms ON (ms.id_msg = t.id_first_msg)
		WHERE t.id_topic = _int
		LIMIT 1|
	SELECT b.id_board, b.name, b.child_level, c.name AS cat_name, c.id_cat
		FROM smf_boards AS b
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
		WHERE {query_see_board}
			AND b.redirect = _string
			AND b.id_board != _int|
	SELECT id_member_started, id_first_msg, approved
		FROM smf_topics
		WHERE id_topic = _int
		LIMIT 1|
	SELECT b.count_posts, b.name, m.subject
		FROM smf_boards AS b
			INNER JOIN smf_topics AS t ON (t.id_topic = _int)
			INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
		WHERE {query_see_board}
			AND b.id_board = _int
			AND b.redirect = _string
		LIMIT 1|
	SELECT count_posts
		FROM smf_boards
		WHERE id_board = _int
		LIMIT 1|
	SELECT id_member
			FROM smf_messages
			WHERE id_topic = _int
				AND approved = _int|
	SELECT id_board, approved, COUNT(*) AS num_topics, SUM(unapproved_posts) AS unapproved_posts,
			SUM(num_replies) AS num_replies
		FROM smf_topics
		WHERE id_topic IN (_array_int)
		GROUP BY id_board, approved|
	SELECT lmr.id_member, lmr.id_msg, t.id_topic
		FROM smf_topics AS t
			INNER JOIN smf_log_mark_read AS lmr ON (lmr.id_board = t.id_board
				AND lmr.id_msg > t.id_first_msg AND lmr.id_msg > _int)
			LEFT JOIN smf_log_topics AS lt ON (lt.id_topic = t.id_topic AND lt.id_member = lmr.id_member)
		WHERE t.id_topic IN (_array_int)
			AND lmr.id_msg > IFNULL(lt.id_msg, 0)|
	SELECT id_msg
			FROM smf_messages
			WHERE id_topic IN (_array_int)
				and approved = _int|
	SELECT id_topic, id_first_msg, id_last_msg
			FROM smf_topics
			WHERE id_topic IN (_array_int)|
	SELECT id_topic, MIN(id_msg) AS first_msg, MAX(id_msg) AS last_msg
			FROM smf_messages
			WHERE id_topic IN (_array_int)
			GROUP BY id_topic|
	SELECT (IFNULL(lb.id_msg, 0) >= b.id_msg_updated) AS isSeen
		FROM smf_boards AS b
			LEFT JOIN smf_log_boards AS lb ON (lb.id_board = b.id_board AND lb.id_member = _int)
		WHERE b.id_board = _int|
	SELECT name
				FROM smf_categories
				WHERE id_cat = _int|
	SELECT b.id_board, b.num_posts
			FROM smf_boards AS b
			WHERE b.id_cat IN (_array_int)
				AND {query_see_board}|
	SELECT num_posts
			FROM smf_boards
			WHERE id_board = _int
			LIMIT 1|
	SELECT id_member, member_name, real_name, date_registered, last_login
		FROM smf_members
		ORDER BY id_member DESC
		LIMIT _int|
	SELECT
			m.smileys_enabled, m.poster_time, m.id_msg, m.subject, m.body, m.id_topic, t.id_board,
			b.name AS bname, t.num_replies, m.id_member, m.icon, mf.id_member AS id_first_member,
			IFNULL(mem.real_name, m.poster_name) AS poster_name, mf.subject AS first_subject,
			IFNULL(memf.real_name, mf.poster_name) AS first_poster_name, mem.hide_email,
			IFNULL(mem.email_address, m.poster_email) AS poster_email, m.modified_time
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
			INNER JOIN smf_messages AS mf ON (mf.id_msg = t.id_first_msg)
			INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
			LEFT JOIN smf_members AS memf ON (memf.id_member = mf.id_member)
		WHERE m.id_msg IN (_array_int)
			|
	SELECT id_member
			FROM smf_log_notify
			WHERE id_member = _int
				AND id_topic = _int
			LIMIT 1|
	SELECT id_member
			FROM smf_log_notify
			WHERE id_member = _int
				AND id_board = _int
			LIMIT 1|
	SELECT id_server, name, url
		FROM smf_package_servers|
	SELECT name, url
			FROM smf_package_servers
			WHERE id_server = _int
			LIMIT 1|
	SELECT name, url
			FROM smf_package_servers
			WHERE id_server = _int
			LIMIT 1|
	SELECT id_theme, variable, value
		FROM smf_themes
		WHERE (id_theme = _int OR id_theme IN (_array_int))
			AND variable IN (_string, _string)|
	SELECT version, themes_installed, db_changes
		FROM smf_log_packages
		WHERE package_id = _string
			AND install_state != _int
		ORDER BY time_installed DESC
		LIMIT 1|
	SELECT id_theme, variable, value
		FROM smf_themes
		WHERE id_theme IN (_array_int)
			AND variable IN (_string, _string)|
	SELECT version, themes_installed, db_changes
		FROM smf_log_packages
		WHERE package_id = _string
			AND install_state != _int
		ORDER BY time_installed DESC
		LIMIT 1|
	SELECT id_install, install_state
			FROM smf_log_packages
			WHERE install_state != _int
				AND package_id = _string
				|
	SELECT id_theme, variable, value
		FROM smf_themes
		WHERE (id_theme = _int OR id_theme IN (_array_int))
			AND variable IN (_string, _string)|
	SELECT value
		FROM smf_themes
		WHERE id_theme > _int
			AND id_member = _int
			AND variable = _string
		ORDER BY value ASC|
	SELECT MAX(max_messages) AS top_limit, MIN(max_messages) AS bottom_limit
			FROM smf_membergroups
			WHERE id_group IN (_array_int)|
	SELECT labels, is_read, COUNT(*) AS num
			FROM smf_pm_recipients
			WHERE id_member = _int
				AND deleted = _int
			GROUP BY labels, is_read|
	SELECT MAX(_raw) AS sort_param, pm.id_pm_head
				FROM smf_personal_messages AS pm|
	SELECT pm.id_pm AS id_pm, pm.id_pm_head
				FROM smf_personal_messages AS pm|
	SELECT MAX(pm.id_pm) AS id_pm, pm.id_pm_head
				FROM smf_personal_messages AS pm|
	SELECT pm.id_pm, pm.id_pm_head, pm.id_member_from
			FROM smf_personal_messages AS pm|
	SELECT pm.id_pm, pm.id_member_from, pm.deleted_by_sender, pmr.id_member, pmr.deleted
				FROM smf_personal_messages AS pm
					INNER JOIN smf_pm_recipients AS pmr ON (pmr.id_pm = pm.id_pm)
				WHERE pm.id_pm_head = _int
					AND ((pm.id_member_from = _int AND pm.deleted_by_sender = _int)
						OR (pmr.id_member = _int AND pmr.deleted = _int))
				ORDER BY pm.id_pm|
	SELECT pmr.id_pm, mem_to.id_member AS id_member_to, mem_to.real_name AS to_name, pmr.bcc, pmr.labels, pmr.is_read
			FROM smf_pm_recipients AS pmr
				LEFT JOIN smf_members AS mem_to ON (mem_to.id_member = pmr.id_member)
			WHERE pmr.id_pm IN (_array_int)|
	SELECT pm.id_pm, pm.subject, pm.id_member_from, pm.body, pm.msgtime, pm.from_name
			FROM smf_personal_messages AS pm|
	SELECT id_member
			FROM smf_members
			WHERE real_name LIKE _raw|
	SELECT
				pmr.id_pm, mem_to.id_member AS id_member_to, mem_to.real_name AS to_name,
				pmr.bcc, pmr.labels, pmr.is_read
			FROM smf_pm_recipients AS pmr
				LEFT JOIN smf_members AS mem_to ON (mem_to.id_member = pmr.id_member)
			WHERE pmr.id_pm IN (_array_int)|
	SELECT COUNT(pr.id_pm) AS post_count
			FROM smf_personal_messages AS pm
				INNER JOIN smf_pm_recipients AS pr ON (pr.id_pm = pm.id_pm)
			WHERE pm.id_member_from = _int
				AND pm.msgtime > _int|
	SELECT
				id_pm
			FROM smf_pm_recipients
			WHERE id_pm = _int
				AND id_member = _int
			LIMIT 1|
	SELECT mem.id_member, mem.real_name
				FROM smf_pm_recipients AS pmr
					INNER JOIN smf_members AS mem ON (mem.id_member = pmr.id_member)
				WHERE pmr.id_pm = _int
					AND pmr.id_member != _int
					AND pmr.bcc = _int|
	SELECT id_member, real_name
			FROM smf_members
			WHERE id_member IN (_array_int)|
	SELECT COUNT(pr.id_pm) AS post_count
			FROM smf_personal_messages AS pm
				INNER JOIN smf_pm_recipients AS pr ON (pr.id_pm = pm.id_pm)
			WHERE pm.id_member_from = _int
				AND pm.msgtime > _int|
	SELECT id_pm_head, id_pm
			FROM smf_personal_messages
			WHERE id_pm IN (_array_int)|
	SELECT id_pm, id_pm_head
			FROM smf_personal_messages
			WHERE id_pm_head IN (_array_int)|
	SELECT id_pm
			FROM smf_personal_messages
			WHERE deleted_by_sender = _int
				AND id_member_from = _int
				AND msgtime < _int|
	SELECT pmr.id_pm
			FROM smf_pm_recipients AS pmr
				INNER JOIN smf_personal_messages AS pm ON (pm.id_pm = pmr.id_pm)
			WHERE pmr.deleted = _int
				AND pmr.id_member = _int
				AND pm.msgtime < _int|
	SELECT id_member, COUNT(*) AS num_deleted_messages, CASE WHEN is_read & 1 >= 1 THEN 1 ELSE 0 END AS is_read
			FROM smf_pm_recipients
			WHERE id_member IN (_array_int)
				AND deleted = _int|
	SELECT pm.id_pm AS sender, pmr.id_pm
		FROM smf_personal_messages AS pm
			LEFT JOIN smf_pm_recipients AS pmr ON (pmr.id_pm = pm.id_pm AND pmr.deleted = _int)
		WHERE pm.deleted_by_sender = _int
			|
	SELECT labels, COUNT(*) AS num
			FROM smf_pm_recipients
			WHERE id_member = _int
				AND NOT (is_read & 1 >= 1)
				AND deleted = _int
			GROUP BY labels|
	SELECT id_pm, labels
				FROM smf_pm_recipients
				WHERE FIND_IN_SET(_raw, labels) != 0
					AND id_member = _int|
	SELECT id_member, real_name
			FROM smf_members
			WHERE id_group = _int OR FIND_IN_SET(_int, additional_groups) != 0
			ORDER BY real_name|
	SELECT pm.subject, pm.body, pm.msgtime, pm.id_member_from, IFNULL(m.real_name, pm.from_name) AS sender_name
			FROM smf_personal_messages AS pm
				INNER JOIN smf_pm_recipients AS pmr ON (pmr.id_pm = pm.id_pm)
				LEFT JOIN smf_members AS m ON (m.id_member = pm.id_member_from)
			WHERE pm.id_pm = _int
				AND pmr.id_member = _int
				AND pmr.deleted = _int
			LIMIT 1|
	SELECT mem_to.id_member AS id_member_to, mem_to.real_name AS to_name, pmr.bcc
			FROM smf_pm_recipients AS pmr
				LEFT JOIN smf_members AS mem_to ON (mem_to.id_member = pmr.id_member)
			WHERE pmr.id_pm = _int
				AND pmr.id_member != _int|
	SELECT id_member, real_name, lngfile
			FROM smf_members
			WHERE (id_group = _int OR FIND_IN_SET(_int, additional_groups) != 0)
				|
	SELECT mg.id_group, mg.group_name, IFNULL(gm.id_member, 0) AS can_moderate, mg.hidden
		FROM smf_membergroups AS mg
			LEFT JOIN smf_group_moderators AS gm ON (gm.id_group = mg.id_group AND gm.id_member = _int)
		WHERE mg.min_posts = _int
			AND mg.id_group != _int
			AND mg.hidden = _int
		ORDER BY mg.group_name|
	SELECT id_member, member_name
					FROM smf_members
					WHERE id_member IN (_array_int)|
	SELECT id_member
					FROM smf_members
					WHERE real_name = _string
						OR member_name = _string|
	SELECT
			pmr.id_pm, pm.id_member_from, pm.subject, pm.body, mem.id_group, pmr.labels
		FROM smf_pm_recipients AS pmr
			INNER JOIN smf_personal_messages AS pm ON (pm.id_pm = pmr.id_pm)
			LEFT JOIN smf_members AS mem ON (mem.id_member = pm.id_member_from)
		WHERE pmr.id_member = _int
			AND pmr.deleted = _int
			|
	SELECT
			id_rule, rule_name, criteria, actions, delete_pm, is_or
		FROM smf_pm_rules
		WHERE id_member = _int|
	SELECT
			pm.id_member_from = _int AND pm.deleted_by_sender = _int AS valid_for_outbox,
			pmr.id_pm IS NOT NULL AS valid_for_inbox
		FROM smf_personal_messages AS pm
			LEFT JOIN smf_pm_recipients AS pmr ON (pmr.id_pm = pm.id_pm AND pmr.id_member = _int AND pmr.deleted = _int)
		WHERE pm.id_pm = _int
			AND ((pm.id_member_from = _int AND pm.deleted_by_sender = _int) OR pmr.id_pm IS NOT NULL)|
	SELECT IFNULL(lp.id_choice, -1) AS selected, p.voting_locked, p.id_poll, p.expire_time, p.max_votes, p.change_vote,
			p.guest_vote, p.reset_poll, p.num_guest_voters
		FROM smf_topics AS t
			INNER JOIN smf_polls AS p ON (p.id_poll = t.id_poll)
			LEFT JOIN smf_log_polls AS lp ON (p.id_poll = lp.id_poll AND lp.id_member = _int AND lp.id_member != _int)
		WHERE t.id_topic = _int
		LIMIT 1|
	SELECT id_choice
			FROM smf_log_polls
			WHERE id_member = _int
				AND id_poll = _int|
	SELECT t.id_member_started, t.id_poll, p.voting_locked
		FROM smf_topics AS t
			INNER JOIN smf_polls AS p ON (p.id_poll = t.id_poll)
		WHERE t.id_topic = _int
		LIMIT 1|
	SELECT
			t.id_member_started, p.id_poll, p.question, p.hide_results, p.expire_time, p.max_votes, p.change_vote,
			m.subject, p.guest_vote, p.id_member AS poll_starter
		FROM smf_topics AS t
			INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
			LEFT JOIN smf_polls AS p ON (p.id_poll = t.id_poll)
		WHERE t.id_topic = _int
		LIMIT 1|
	SELECT label, votes, id_choice
				FROM smf_poll_choices
				WHERE id_poll = _int|
	SELECT label, votes, id_choice
				FROM smf_poll_choices
				WHERE id_poll = _int|
	SELECT t.id_member_started, t.id_poll, p.id_member AS poll_starter, p.expire_time
		FROM smf_topics AS t
			LEFT JOIN smf_polls AS p ON (p.id_poll = t.id_poll)
		WHERE t.id_topic = _int
		LIMIT 1|
	SELECT id_choice
		FROM smf_poll_choices
		WHERE id_poll = _int|
	SELECT t.id_member_started, p.id_member AS poll_starter
			FROM smf_topics AS t
				INNER JOIN smf_polls AS p ON (p.id_poll = t.id_poll)
			WHERE t.id_topic = _int
			LIMIT 1|
	SELECT id_poll
		FROM smf_topics
		WHERE id_topic = _int
		LIMIT 1|
	SELECT m.id_msg, m.id_member, m.id_board, t.id_topic, t.id_first_msg, t.id_member_started
			FROM smf_messages AS m
				INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
			LEFT JOIN smf_boards AS b ON (t.id_board = b.id_board)
			WHERE m.id_msg IN (_array_int)
				AND m.approved = _int
				AND {query_see_board}|
	SELECT COUNT(*)
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic AND t.id_first_msg != m.id_msg)
			INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
		WHERE m.approved = _int
			AND {query_see_board}
			|
	SELECT COUNT(m.id_topic)
		FROM smf_topics AS m
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
		WHERE m.approved = _int
			AND {query_see_board}
			|
	SELECT m.id_msg, m.id_topic, m.id_board, m.subject, m.body, m.id_member,
			IFNULL(mem.real_name, m.poster_name) AS poster_name, m.poster_time, m.smileys_enabled,
			t.id_member_started, t.id_first_msg, b.name AS board_name, c.id_cat, c.name AS cat_name
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
		WHERE m.approved = _int
			AND t.id_first_msg |
	SELECT a.id_attach
			FROM smf_attachments AS a
				INNER JOIN smf_messages AS m ON (m.id_msg = a.id_msg)
				LEFT JOIN smf_boards AS b ON (m.id_board = b.id_board)
			WHERE a.id_attach IN (_array_int)
				AND a.approved = _int
				AND a.attachment_type = _int
				AND {query_see_board}
				|
	SELECT COUNT(*)
		FROM smf_attachments AS a
			INNER JOIN smf_messages AS m ON (m.id_msg = a.id_msg)
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
		WHERE a.approved = _int
			AND a.attachment_type = _int
			AND {query_see_board}
			|
	SELECT a.id_attach, a.filename, a.size, m.id_msg, m.id_topic, m.id_board, m.subject, m.body, m.id_member,
			IFNULL(mem.real_name, m.poster_name) AS poster_name, m.poster_time,
			t.id_member_started, t.id_first_msg, b.name AS board_name, c.id_cat, c.name AS cat_name
		FROM smf_attachments AS a
			INNER JOIN smf_messages AS m ON (m.id_msg = a.id_msg)
			INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
		WHERE a.approved = _int
			AND a.attachment_type = _int
			AND {query_see_board}
			|
	SELECT t.id_member_started, t.id_first_msg, m.id_member, m.subject, m.approved
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = _int)
		WHERE m.id_msg = _int
			AND m.id_topic = _int
		LIMIT 1|
	SELECT id_msg
		FROM smf_messages
		WHERE approved = _int|
	SELECT id_attach
		FROM smf_attachments
		WHERE approved = _int|
	SELECT id_topic
			FROM smf_messages
			WHERE id_msg = _int|
	SELECT
				t.locked, IFNULL(ln.id_topic, 0) AS notify, t.is_sticky, t.id_poll, t.num_replies, mf.id_member,
				t.id_first_msg, mf.subject,
				CASE WHEN ml.poster_time > ml.modified_time THEN ml.poster_time ELSE ml.modified_time END AS last_post_time
			FROM smf_topics AS t
				LEFT JOIN smf_log_notify AS ln ON (ln.id_topic = t.id_topic AND ln.id_member = _int)
				LEFT JOIN smf_messages AS mf ON (mf.id_msg = t.id_first_msg)
				LEFT JOIN smf_messages AS ml ON (ml.id_msg = t.id_last_msg)
			WHERE t.id_topic = _int
			LIMIT 1|
	SELECT
					id_member, title, MONTH(start_date) AS month, DAYOFMONTH(start_date) AS day,
					YEAR(start_date) AS year, (TO_DAYS(end_date) - TO_DAYS(start_date)) AS span
				FROM smf_calendar
				WHERE id_event = _int
				LIMIT 1|
	SELECT
					m.id_member, m.modified_time, m.smileys_enabled, m.body,
					m.poster_name, m.poster_email, m.subject, m.icon, m.approved,
					IFNULL(a.size, -1) AS filesize, a.filename, a.id_attach,
					a.approved AS attachment_approved, t.id_member_started AS id_member_poster,
					m.poster_time
			FROM smf_messages AS m
					INNER JOIN smf_topics AS t ON (t.id_topic = _int)
					LEFT JOIN smf_attachments AS a ON (a.id_msg = m.id_msg AND a.attachment_type = _int)
				WHERE m.id_msg = _int
					AND m.id_topic = _int|
	SELECT IFNULL(size, -1) AS filesize, filename, id_attach, approved
					FROM smf_attachments
					WHERE id_msg = _int
						AND attachment_type = _int|
	SELECT id_member, poster_name, poster_email
					FROM smf_messages
					WHERE id_msg = _int
						AND id_topic = _int
					LIMIT 1|
	SELECT
				m.id_member, m.modified_time, m.smileys_enabled, m.body,
				m.poster_name, m.poster_email, m.subject, m.icon, m.approved,
				IFNULL(a.size, -1) AS filesize, a.filename, a.id_attach,
				a.approved AS attachment_approved, t.id_member_started AS id_member_poster,
				m.poster_time
			FROM smf_messages AS m
				INNER JOIN smf_topics AS t ON (t.id_topic = _int)
				LEFT JOIN smf_attachments AS a ON (a.id_msg = m.id_msg AND a.attachment_type = _int)
			WHERE m.id_msg = _int
				AND m.id_topic = _int|
	SELECT m.subject, IFNULL(mem.real_name, m.poster_name) AS poster_name, m.poster_time, m.body
				FROM smf_messages AS m
					INNER JOIN smf_boards AS b ON (b.id_board = m.id_board AND {query_see_board})
					LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
				WHERE m.id_msg = _int|
	SELECT COUNT(*), SUM(size)
				FROM smf_attachments
				WHERE id_msg = _int
					AND attachment_type = _int|
	SELECT locked, is_sticky, id_poll, approved, num_replies, id_first_msg, id_member_started, id_board
			FROM smf_topics
			WHERE id_topic = _int
			LIMIT 1|
	SELECT id_member, poster_name, poster_email, poster_time, approved
			FROM smf_messages
			WHERE id_msg = _int
			LIMIT 1|
	SELECT COUNT(*), SUM(size)
				FROM smf_attachments
				WHERE id_msg = _int
					AND attachment_type = _int|
	SELECT id_member
				FROM smf_calendar
				WHERE id_event = _int|
	SELECT mg.id_group, COUNT(mem.id_member) AS num_members
		FROM smf_membergroups AS mg
			LEFT JOIN smf_members AS mem ON (mem.id_group = mg.id_group OR FIND_IN_SET(mg.id_group, mem.additional_groups) != 0 OR mg.id_group = mem.id_post_group)
		WHERE mg.id_group IN (_array_int)
		GROUP BY mg.id_group|
	SELECT id_group, group_name
		FROM smf_membergroups
		WHERE id_group IN (_array_int)|
	SELECT m.subject
		FROM smf_topics AS t
			INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
		WHERE t.id_topic = _int|
	SELECT m.id_msg, m.subject, m.body
		FROM smf_topics AS t
			INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
		WHERE t.id_topic = _int|
	SELECT mem.id_member, mem.email_address, mem.lngfile
		FROM smf_members AS mem
		WHERE mem.id_member != _int|
	SELECT
			mem.id_member, mem.email_address, mem.notify_regularity, mem.notify_send_body, mem.lngfile,
			ln.sent, ln.id_board, mem.id_group, mem.additional_groups, b.member_groups,
			mem.id_post_group
		FROM smf_log_notify AS ln
			INNER JOIN smf_boards AS b ON (b.id_board = ln.id_board)
			INNER JOIN smf_members AS mem ON (mem.id_member = ln.id_member)
		WHERE ln.id_board IN (_array_int)
			AND mem.id_member != _int
			AND mem.is_activated = _int
			AND mem.notify_types != _int
			AND mem.notify_regularity < _int
		ORDER BY mem.lngfile|
	SELECT IFNULL(mem.real_name, m.poster_name) AS poster_name, m.poster_time, m.body, m.smileys_enabled, m.id_msg
		FROM smf_messages AS m
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE m.id_topic = _int|
	SELECT IFNULL(mem.real_name, m.poster_name) AS poster_name, m.poster_time, m.body, m.id_topic, m.subject,
			m.id_board, m.id_member, m.approved
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board AND {query_see_board})
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE m.id_msg = _int|
	SELECT
				t.locked, t.num_replies, t.id_member_started, t.id_first_msg,
				m.id_msg, m.id_member, m.poster_time, m.subject, m.smileys_enabled, m.body, m.icon,
				m.modified_time, m.modified_name, m.approved
			FROM smf_messages AS m
				INNER JOIN smf_topics AS t ON (t.id_topic = _int)
			WHERE m.id_msg = _raw
				AND m.id_topic = _int|
	SELECT m.poster_time, IFNULL(mem.real_name, m.poster_name) AS poster_name
		FROM smf_messages AS m
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE m.id_topic = _int
		ORDER BY m.id_msg
		LIMIT 1|
	SELECT subject, poster_time, body, IFNULL(mem.real_name, poster_name) AS poster_name
		FROM smf_messages AS m
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE m.id_topic = _int
		ORDER BY m.id_msg|
	SELECT SUM(counter)
			FROM smf_log_comments
			WHERE id_recipient = _int
				AND id_member = _int
				AND comment_type = _string
				AND log_time > _int|
	SELECT subject
			FROM smf_messages AS m
				INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
			WHERE id_msg = _int
				AND {query_see_board}
			LIMIT 1|
	SELECT recipient_name AS template_title, body
		FROM smf_log_comments
		WHERE comment_type = _string
			AND (id_recipient = _int OR id_recipient = _int)|
	SELECT COUNT(*)
		FROM smf_log_comments
		WHERE id_recipient = _int
			AND comment_type = _string|
	SELECT id_member
			FROM smf_members
			WHERE (id_group = _int OR FIND_IN_SET(_int, additional_groups) != 0)
				AND id_member != _int
			LIMIT 1|
	SELECT t.id_topic
					FROM smf_topics AS t
					WHERE t.id_member_started = _int|
	SELECT m.id_msg
				FROM smf_messages AS m
					INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic
						AND t.id_first_msg != m.id_msg)
				WHERE m.id_member = _int|
	SELECT id_sublog, id_subscribe, start_time, end_time, status, payments_pending, pending_details
		FROM smf_log_subscribed
		WHERE id_member = _int|
	SELECT col_name
		FROM smf_custom_fields
		WHERE active = _int|
	SELECT id_member
				FROM smf_members
				WHERE member_name IN (_array_string) OR real_name IN (_array_string)
				LIMIT _int|
	SELECT id_member
			FROM smf_members
			WHERE id_member IN (_array_int)
			ORDER BY real_name
			LIMIT _int|
	SELECT id_member
				FROM smf_members
				WHERE member_name IN (_array_string) OR real_name IN (_array_string)
				LIMIT _int|
	SELECT id_member
			FROM smf_members
			WHERE id_member IN (_array_int)
			ORDER BY real_name
			LIMIT _int|
	SELECT COUNT(*)
		FROM smf_log_notify AS ln|
	SELECT id_member, variable, value
			FROM smf_themes
			WHERE id_theme IN (1, _int)
				AND id_member IN (-1, _int)|
	SELECT group_name, id_group, hidden
		FROM smf_membergroups
		WHERE id_group != _int
			AND min_posts = _int
		ORDER BY min_posts, CASE WHEN id_group < _int THEN id_group ELSE 4 END, group_name|
	SELECT id_member
				FROM smf_members
				WHERE (id_group = _int OR FIND_IN_SET(_int, additional_groups) != 0)
					AND id_member != _int
				LIMIT 1|
	SELECT mg.id_group, mg.group_name, mg.description, mg.group_type, mg.online_color, mg.hidden,
			IFNULL(lgr.id_member, 0) AS pending
		FROM smf_membergroups AS mg
			LEFT JOIN smf_log_group_requests AS lgr ON (lgr.id_member = _int AND lgr.id_group = mg.id_group)
		WHERE (mg.id_group IN (_array_int)
			OR mg.group_type > _int)
			AND mg.min_posts = _int
			AND mg.id_group != _int
		ORDER BY group_name|
	SELECT id_group, group_type, hidden, group_name
		FROM smf_membergroups
		WHERE id_group IN (_int, _int)|
	SELECT COUNT(permission)
			FROM smf_permissions
			WHERE id_group = _int
				AND permission = _string
				AND add_deny = _int|
	SELECT id_member
			FROM smf_log_group_requests
			WHERE id_member = _int
				AND id_group = _int|
	SELECT id_member
			FROM smf_group_moderators
			WHERE id_group = _int|
	SELECT id_member, email_address, lngfile, member_name, mod_prefs
				FROM smf_members
				WHERE id_member IN (_array_int)
					AND notify_types != _int
				ORDER BY lngfile|
	SELECT subject, id_member, id_topic, id_board
			FROM smf_messages
			WHERE id_msg = _int|
	SELECT COUNT(*)
			FROM smf_topics AS t|
	SELECT COUNT(*)
			FROM smf_messages AS m|
	SELECT MIN(id_msg), MAX(id_msg)
		FROM smf_messages AS m
		WHERE m.id_member = _int|
	SELECT
					b.id_board, b.name AS bname, c.id_cat, c.name AS cname, t.id_member_started, t.id_first_msg, t.id_last_msg,
					t.approved, m.body, m.smileys_enabled, m.subject, m.poster_time, m.id_topic, m.id_msg
				FROM smf_topics AS t
					INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
					LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
					INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
				WHERE t.id_member_started = _int|
	SELECT
					b.id_board, b.name AS bname, c.id_cat, c.name AS cname, m.id_topic, m.id_msg,
					t.id_member_started, t.id_first_msg, t.id_last_msg, m.body, m.smileys_enabled,
					m.subject, m.poster_time, m.approved
				FROM smf_messages AS m
					INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
					INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
					LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
				WHERE m.id_member = _int|
	SELECT COUNT(*)
		FROM smf_attachments AS a
			INNER JOIN smf_messages AS m ON (m.id_msg = a.id_msg)
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board AND {query_see_board})
		WHERE a.attachment_type = _int
			AND a.id_msg != _int
			AND m.id_member = _int|
	SELECT a.id_attach, a.id_msg, a.filename, a.downloads, a.approved, m.id_msg, m.id_topic,
			m.id_board, m.poster_time, m.subject, b.name
		FROM smf_attachments AS a
			INNER JOIN smf_messages AS m ON (m.id_msg = a.id_msg)
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board AND {query_see_board})
		WHERE a.attachment_type = _int
			AND a.id_msg != _int
			AND m.id_member = _int|
	SELECT COUNT(*)
		FROM smf_topics
		WHERE id_member_started = _int|
	SELECT COUNT(*)
		FROM smf_topics
		WHERE id_member_started = _int|
	SELECT COUNT(DISTINCT id_poll)
		FROM smf_log_polls
		WHERE id_member = _int|
	SELECT
			b.id_board, MAX(b.name) AS name, MAX(b.num_posts) AS num_posts, COUNT(*) AS message_count
		FROM smf_messages AS m
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
		WHERE m.id_member = _int
			AND b.count_posts = _int
			AND {query_see_board}
		GROUP BY b.id_board
		ORDER BY message_count DESC
		LIMIT 10|
	SELECT
			b.id_board, MAX(b.name) AS name, b.num_posts, COUNT(*) AS message_count,
			CASE WHEN COUNT(*) > MAX(b.num_posts) THEN 1 ELSE COUNT(*) / MAX(b.num_posts) END * 100 AS percentage
		FROM smf_messages AS m
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
		WHERE m.id_member = _int
			AND {query_see_board}
		GROUP BY b.id_board
		ORDER BY percentage DESC
		LIMIT 10|
	SELECT
			HOUR(FROM_UNIXTIME(poster_time + _int)) AS hour,
			COUNT(*) AS post_count
		FROM smf_messages
		WHERE id_member = _int|
	SELECT MAX(id_msg)
			FROM smf_messages AS m
			WHERE m.id_member = _int|
	SELECT poster_ip
		FROM smf_messages
		WHERE id_member = _int
		|
	SELECT COUNT(*) AS error_count, ip
		FROM smf_log_errors
		WHERE id_member = _int
		GROUP BY ip|
	SELECT mem.id_member
			FROM smf_messages AS m
				INNER JOIN smf_members AS mem ON (mem.id_member = m.id_member)
			WHERE m.poster_ip IN (_array_string)
			GROUP BY mem.id_member
			HAVING mem.id_member != _int|
	SELECT id_member, real_name
				FROM smf_members
				WHERE id_member IN (_array_int)|
	SELECT id_member, real_name
			FROM smf_members
			WHERE id_member != _int
				AND member_ip IN (_array_string)|
	SELECT id_member, real_name AS display_name, member_ip
		FROM smf_members
		WHERE member_ip |
	SELECT col_name, field_name, bbc
		FROM smf_custom_fields|
	SELECT COUNT(*) AS edit_count
		FROM smf_log_actions
		WHERE id_log = _int
			AND id_member = _int|
	SELECT
				id_member, real_name
			FROM smf_members
			WHERE id_member IN (_array_int)|
	SELECT b.id_board, b.name, b.id_profile, b.member_groups, IFNULL(mods.id_member, 0) AS is_mod
		FROM smf_boards AS b
			LEFT JOIN smf_moderators AS mods ON (mods.id_board = b.id_board AND mods.id_member = _int)
		WHERE {query_see_board}|
	SELECT p.permission, p.add_deny, mg.group_name, p.id_group
		FROM smf_permissions AS p
			LEFT JOIN smf_membergroups AS mg ON (mg.id_group = p.id_group)
		WHERE p.id_group IN (_array_int)
		ORDER BY p.add_deny DESC, p.permission, mg.min_posts, CASE WHEN mg.id_group < _int THEN mg.id_group ELSE 4 END, mg.group_name|
	SELECT ml.poster_time, ml.subject, ml.id_topic, ml.poster_name, SUBSTRING(ml.body, 1, 385) AS body,
			ml.smileys_enabled
		FROM smf_boards AS b
			INNER JOIN smf_messages AS ml ON (ml.id_msg = b.id_last_msg)
		WHERE {query_wanna_see_board}|
	SELECT name
				FROM smf_categories
				WHERE id_cat = _int
				LIMIT 1|
	SELECT b.id_board, b.num_posts
			FROM smf_boards AS b
			WHERE b.id_cat IN (_array_int)
				AND {query_see_board}|
	SELECT b.id_board, b.num_posts
			FROM smf_boards AS b
			WHERE b.id_board IN (_array_int)
				AND {query_see_board}
			LIMIT _int|
	SELECT num_posts
			FROM smf_boards
			WHERE id_board = _int
			LIMIT 1|
	SELECT b.id_board, b.id_parent
			FROM smf_boards AS b
			WHERE {query_wanna_see_board}
				AND b.child_level > _int
				AND b.id_board NOT IN (_array_int)
			ORDER BY child_level ASC
			|
	SELECT b.id_board
			FROM smf_boards AS b
			WHERE {query_see_board}
				AND b.id_board IN (_array_int)|
	SELECT name
			FROM smf_categories
			WHERE id_cat = _int
			LIMIT 1|
	SELECT MIN(id_msg)
				FROM smf_log_mark_read
				WHERE id_member = _int
					AND id_board = _int|
	SELECT MIN(lmr.id_msg)
				FROM smf_boards AS b
					LEFT JOIN smf_log_mark_read AS lmr ON (lmr.id_board = b.id_board AND lmr.id_member = _int)
				WHERE {query_see_board}|
	SELECT MIN(id_msg)
					FROM smf_log_topics
					WHERE id_member = _int|
	SELECT COUNT(*), MIN(t.id_last_msg)
			FROM smf_topics AS t|
	SELECT DISTINCT t.id_topic
				FROM smf_topics AS t
					INNER JOIN smf_messages AS m ON (m.id_topic = t.id_topic AND m.id_member = _int)|
	SELECT id_topic
			FROM smf_messages
			WHERE id_topic IN (_array_int)
				AND id_member = _int|
	SELECT col_name, field_name, field_type, field_length, mask, show_reg
		FROM smf_custom_fields
		WHERE active = _int|
	SELECT id_member, validation_code, member_name, real_name, email_address, is_activated, passwd, lngfile
		FROM smf_members|
	SELECT id_member
			FROM smf_members
			WHERE email_address = _string
			LIMIT 1|
	SELECT member_name
		FROM smf_members
		WHERE id_member = _int
			AND is_activated = _int|
	SELECT id_member, real_name, member_name, email_address, is_activated, validation_code, lngfile, openid_uri, secret_question
			FROM smf_members
			WHERE email_address = _string
			LIMIT 1|
	SELECT validation_code, member_name, email_address, passwd_flood
		FROM smf_members
		WHERE id_member = _int
			AND is_activated = _int
			AND validation_code != _string
		LIMIT 1|
	SELECT id_member, real_name, member_name, secret_question, openid_uri
		FROM smf_members
		WHERE id_member = _int
		LIMIT 1|
	SELECT id_member, real_name, member_name, secret_answer, secret_question, openid_uri, email_address
		FROM smf_members
		WHERE id_member = _int
		LIMIT 1|
	SELECT t.id_member_started, ms.subject, t.approved
		FROM smf_topics AS t
			INNER JOIN smf_messages AS ms ON (ms.id_msg = t.id_first_msg)
		WHERE t.id_topic = _int
		LIMIT 1|
	SELECT t.id_member_started, m.id_member, m.subject, m.poster_time, m.approved
		FROM smf_topics AS t
			INNER JOIN smf_messages AS m ON (m.id_msg = _int AND m.id_topic = _int)
		WHERE t.id_topic = _int
		LIMIT 1|
	SELECT t.id_topic
		FROM smf_topics AS t
			INNER JOIN smf_messages AS m ON (m.id_msg = t.id_last_msg)
		WHERE
			m.poster_time < _int|
	SELECT m.id_member, COUNT(*) AS posts
			FROM smf_messages AS m
				INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
			WHERE m.id_topic IN (_array_int)
				AND m.icon != _string
				AND b.count_posts = _int
				AND m.approved = _int
			GROUP BY m.id_member|
	SELECT id_board, approved, COUNT(*) AS num_topics, SUM(unapproved_posts) AS unapproved_posts,
			SUM(num_replies) AS num_replies
		FROM smf_topics
		WHERE id_topic IN (_array_int)
		GROUP BY id_board, approved|
	SELECT id_msg, body
			FROM smf_messages
			WHERE id_topic IN (_array_int)|
	SELECT (IFNULL(lb.id_msg, 0) >= b.id_msg_updated) AS is_seen, id_last_msg
			FROM smf_boards AS b
				LEFT JOIN smf_log_boards AS lb ON (lb.id_board = b.id_board AND lb.id_member = _int)
			WHERE b.id_board = _int|
	SELECT id_topic, id_first_msg, id_last_msg
			FROM smf_topics
			WHERE id_previous_topic = _int
				AND id_board = _int|
	SELECT m.id_topic, m.id_msg, m.id_board, m.subject, m.id_member, t.id_previous_board, t.id_previous_topic,
				t.id_first_msg, b.count_posts, IFNULL(pt.id_board, 0) AS possible_prev_board
			FROM smf_messages AS m
				INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
				INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
				LEFT JOIN smf_topics AS pt ON (pt.id_topic = t.id_previous_topic)
			WHERE m.id_msg IN (_array_int)|
	SELECT t.id_topic, t.id_board, m.subject
				FROM smf_topics AS t
					INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
				WHERE t.id_topic IN (_array_int)|
	SELECT t.id_topic, t.id_previous_board, t.id_board, t.id_first_msg, m.subject
			FROM smf_topics AS t
				INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
			WHERE t.id_topic IN (_array_int)|
	SELECT count_posts
				FROM smf_boards
				WHERE id_board = _int|
	SELECT id_member, COUNT(id_msg) AS post_count
					FROM smf_messages
					WHERE id_topic = _int
						AND approved = _int
					GROUP BY id_member|
	SELECT t.id_board, t.id_first_msg, t.num_replies, t.unapproved_posts
		FROM smf_topics AS t
			INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
		WHERE t.id_topic = _int|
	SELECT t.id_board, t.id_first_msg, t.num_replies, t.unapproved_posts, b.count_posts
		FROM smf_topics AS t
			INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
		WHERE t.id_topic = _int|
	SELECT id_member
			FROM smf_messages
			WHERE id_msg IN (_array_int)
				AND approved = _int|
	SELECT MIN(id_msg) AS id_first_msg, MAX(id_msg) AS id_last_msg, COUNT(*) AS message_count, approved
		FROM smf_messages
		WHERE id_topic = _int
		GROUP BY id_topic, approved
		ORDER BY approved ASC
		LIMIT 2|
	SELECT id_topic
		FROM smf_messages
		WHERE id_topic = _int|
	SELECT MIN(id_msg) AS id_first_msg, MAX(id_msg) AS id_last_msg, COUNT(*) AS message_count, approved, subject
			FROM smf_messages
			WHERE id_topic = _int
			GROUP BY id_topic, approved
			ORDER BY approved ASC
			LIMIT 2|
	SELECT id_topic, subject
			FROM smf_messages
			WHERE id_msg IN (_array_int)|
	SELECT COUNT(*)
				FROM smf_topics
				WHERE id_topic = 0|
	SELECT COUNT(*)
				FROM smf_messages
				WHERE id_msg = 0|
	SELECT MAX(id_topic)
					FROM smf_messages|
	SELECT m.id_topic, m.id_msg
				FROM smf_messages AS m
					LEFT JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
				WHERE m.id_topic BETWEEN _step_low AND _step_high
					AND t.id_topic IS NULL
				ORDER BY m.id_topic, m.id_msg|
	SELECT
					m.id_board, m.id_topic, MIN(m.id_msg) AS myid_first_msg, MAX(m.id_msg) AS myid_last_msg,
					COUNT(*) - 1 AS my_num_replies
				FROM smf_messages AS m
					LEFT JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
				WHERE t.id_topic IS NULL
				GROUP BY m.id_topic, m.id_board|
	SELECT MAX(id_topic)
					FROM smf_topics|
	SELECT t.id_topic, COUNT(m.id_msg) AS num_msg
				FROM smf_topics AS t
					LEFT JOIN smf_messages AS m ON (m.id_topic = t.id_topic)
				WHERE t.id_topic BETWEEN _step_low AND _step_high
				GROUP BY t.id_topic
				HAVING COUNT(m.id_msg) = 0|
	SELECT MAX(id_poll)
					FROM smf_polls|
	SELECT p.id_poll, p.id_member, p.poster_name, t.id_board
				FROM smf_polls AS p
					LEFT JOIN smf_topics AS t ON (t.id_poll = p.id_poll)
				WHERE p.id_poll BETWEEN _step_low AND _step_high
					AND t.id_poll IS NULL|
	SELECT MAX(id_topic)
					FROM smf_topics|
	SELECT
					t.id_topic, t.id_first_msg, t.id_last_msg,
					CASE WHEN MIN(ma.id_msg) > 0 THEN
						CASE WHEN MIN(mu.id_msg) > 0 THEN
							CASE WHEN MIN(mu.id_msg) < MIN(ma.id_msg) THEN MIN(mu.id_msg) ELSE MIN(ma.id_msg) END ELSE
						MIN(ma.id_msg) END ELSE
					MIN(mu.id_msg) END AS myid_first_msg,
					CASE WHEN MAX(ma.id_msg) > 0 THEN MAX(ma.id_msg) ELSE MIN(mu.id_msg) END AS myid_last_msg,
					t.approved, mf.approved, mf.approved AS firstmsg_approved
				FROM smf_topics AS t
					LEFT JOIN smf_messages AS ma ON (ma.id_topic = t.id_topic AND ma.approved = 1)
					LEFT JOIN smf_messages AS mu ON (mu.id_topic = t.id_topic AND mu.approved = 0)
					LEFT JOIN smf_messages AS mf ON (mf.id_msg = t.id_first_msg)
				WHERE t.id_topic BETWEEN _step_low AND _step_high
				GROUP BY t.id_topic, t.id_first_msg, t.id_last_msg, t.approved, mf.approved
				ORDER BY t.id_topic|
	SELECT MAX(id_topic)
					FROM smf_topics|
	SELECT
					t.id_topic, t.num_replies, mf.approved,
					CASE WHEN COUNT(ma.id_msg) > 0 THEN CASE WHEN mf.approved > 0 THEN COUNT(ma.id_msg) - 1 ELSE COUNT(ma.id_msg) END ELSE 0 END AS my_num_replies
				FROM smf_topics AS t
					LEFT JOIN smf_messages AS ma ON (ma.id_topic = t.id_topic AND ma.approved = 1)
					LEFT JOIN smf_messages AS mf ON (mf.id_msg = t.id_first_msg)
				WHERE t.id_topic BETWEEN _step_low AND _step_high
				GROUP BY t.id_topic, t.num_replies, mf.approved
				ORDER BY t.id_topic|
	SELECT MAX(id_topic)
					FROM smf_topics|
	SELECT
					t.id_topic, t.unapproved_posts, COUNT(mu.id_msg) AS my_unapproved_posts
				FROM smf_topics AS t
					LEFT JOIN smf_messages AS mu ON (mu.id_topic = t.id_topic AND mu.approved = 0)
				WHERE t.id_topic BETWEEN _step_low AND _step_high
				GROUP BY t.id_topic, t.unapproved_posts
				HAVING unapproved_posts != COUNT(mu.id_msg)
				ORDER BY t.id_topic|
	SELECT MAX(id_topic)
					FROM smf_topics|
	SELECT t.id_topic, t.id_board
				FROM smf_topics AS t
					LEFT JOIN smf_boards AS b ON (b.id_board = t.id_board)
				WHERE b.id_board IS NULL
					AND t.id_topic BETWEEN _step_low AND _step_high
				ORDER BY t.id_board, t.id_topic|
	SELECT t.id_board, COUNT(*) AS my_num_topics, COUNT(m.id_msg) AS my_num_posts
				FROM smf_topics AS t
					LEFT JOIN smf_boards AS b ON (b.id_board = t.id_board)
					LEFT JOIN smf_messages AS m ON (m.id_topic = t.id_topic)
				WHERE b.id_board IS NULL
					AND t.id_topic BETWEEN _step_low AND _step_high
				GROUP BY t.id_board|
	SELECT b.id_board, b.id_cat
				FROM smf_boards AS b
					LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
				WHERE c.id_cat IS NULL
				ORDER BY b.id_cat, b.id_board|
	SELECT MAX(id_msg)
					FROM smf_messages|
	SELECT m.id_msg, m.id_member
				FROM smf_messages AS m
					LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
				WHERE mem.id_member IS NULL
					AND m.id_member != 0
					AND m.id_msg BETWEEN _step_low AND _step_high
				ORDER BY m.id_msg|
	SELECT b.id_board, b.id_parent
				FROM smf_boards AS b
					LEFT JOIN smf_boards AS p ON (p.id_board = b.id_parent)
				WHERE b.id_parent != 0
					AND (p.id_board IS NULL OR p.id_board = b.id_board)
				ORDER BY b.id_parent, b.id_board|
	SELECT MAX(id_poll)
					FROM smf_topics|
	SELECT t.id_poll, t.id_topic
				FROM smf_topics AS t
					LEFT JOIN smf_polls AS p ON (p.id_poll = t.id_poll)
				WHERE t.id_poll != 0
					AND t.id_poll BETWEEN _step_low AND _step_high
					AND p.id_poll IS NULL|
	SELECT MAX(id_topic)
					FROM smf_calendar|
	SELECT cal.id_topic, cal.id_event
				FROM smf_calendar AS cal
					LEFT JOIN smf_topics AS t ON (t.id_topic = cal.id_topic)
				WHERE cal.id_topic != 0
					AND cal.id_topic BETWEEN _step_low AND _step_high
					AND t.id_topic IS NULL
				ORDER BY cal.id_topic|
	SELECT MAX(id_member)
					FROM smf_log_topics|
	SELECT lt.id_topic
				FROM smf_log_topics AS lt
					LEFT JOIN smf_topics AS t ON (t.id_topic = lt.id_topic)
				WHERE t.id_topic IS NULL
					AND lt.id_member BETWEEN _step_low AND _step_high|
	SELECT MAX(id_member)
					FROM smf_log_topics|
	SELECT lt.id_member
				FROM smf_log_topics AS lt
					LEFT JOIN smf_members AS mem ON (mem.id_member = lt.id_member)
				WHERE mem.id_member IS NULL
					AND lt.id_member BETWEEN _step_low AND _step_high
				GROUP BY lt.id_member|
	SELECT MAX(id_member)
					FROM smf_log_boards|
	SELECT lb.id_board
				FROM smf_log_boards AS lb
					LEFT JOIN smf_boards AS b ON (b.id_board = lb.id_board)
				WHERE b.id_board IS NULL
					AND lb.id_member BETWEEN _step_low AND _step_high
				GROUP BY lb.id_board|
	SELECT MAX(id_member)
					FROM smf_log_boards|
	SELECT lb.id_member
				FROM smf_log_boards AS lb
					LEFT JOIN smf_members AS mem ON (mem.id_member = lb.id_member)
				WHERE mem.id_member IS NULL
					AND lb.id_member BETWEEN _step_low AND _step_high
				GROUP BY lb.id_member|
	SELECT MAX(id_member)
					FROM smf_log_mark_read|
	SELECT lmr.id_board
				FROM smf_log_mark_read AS lmr
					LEFT JOIN smf_boards AS b ON (b.id_board = lmr.id_board)
				WHERE b.id_board IS NULL
					AND lmr.id_member BETWEEN _step_low AND _step_high
				GROUP BY lmr.id_board|
	SELECT MAX(id_member)
					FROM smf_log_mark_read|
	SELECT lmr.id_member
				FROM smf_log_mark_read AS lmr
					LEFT JOIN smf_members AS mem ON (mem.id_member = lmr.id_member)
				WHERE mem.id_member IS NULL
					AND lmr.id_member BETWEEN _step_low AND _step_high
				GROUP BY lmr.id_member|
	SELECT MAX(id_pm)
					FROM smf_pm_recipients|
	SELECT pmr.id_pm
				FROM smf_pm_recipients AS pmr
					LEFT JOIN smf_personal_messages AS pm ON (pm.id_pm = pmr.id_pm)
				WHERE pm.id_pm IS NULL
					AND pmr.id_pm BETWEEN _step_low AND _step_high
				GROUP BY pmr.id_pm|
	SELECT MAX(id_member)
					FROM smf_pm_recipients|
	SELECT pmr.id_member
				FROM smf_pm_recipients AS pmr
					LEFT JOIN smf_members AS mem ON (mem.id_member = pmr.id_member)
				WHERE pmr.id_member != 0
					AND pmr.id_member BETWEEN _step_low AND _step_high
					AND mem.id_member IS NULL
				GROUP BY pmr.id_member|
	SELECT MAX(id_pm)
					FROM smf_personal_messages|
	SELECT pm.id_pm, pm.id_member_from
				FROM smf_personal_messages AS pm
					LEFT JOIN smf_members AS mem ON (mem.id_member = pm.id_member_from)
				WHERE pm.id_member_from != 0
					AND pm.id_pm BETWEEN _step_low AND _step_high
					AND mem.id_member IS NULL|
	SELECT MAX(id_member)
					FROM smf_log_notify|
	SELECT ln.id_member
				FROM smf_log_notify AS ln
					LEFT JOIN smf_members AS mem ON (mem.id_member = ln.id_member)
				WHERE ln.id_member BETWEEN _step_low AND _step_high
					AND mem.id_member IS NULL
				GROUP BY ln.id_member|
	SELECT MAX(id_topic)
					FROM smf_topics|
	SELECT t.id_topic, fm.subject
				FROM smf_topics AS t
					INNER JOIN smf_messages AS fm ON (fm.id_msg = t.id_first_msg)
					LEFT JOIN smf_log_search_subjects AS lss ON (lss.id_topic = t.id_topic)
				WHERE t.id_topic BETWEEN _step_low AND _step_high
					AND lss.id_topic IS NULL|
	SELECT MAX(id_topic)
					FROM smf_log_search_subjects|
	SELECT lss.id_topic, lss.word
				FROM smf_log_search_subjects AS lss
					LEFT JOIN smf_topics AS t ON (t.id_topic = lss.id_topic)
				WHERE lss.id_topic BETWEEN _step_low AND _step_high
					AND t.id_topic IS NULL|
	SELECT MAX(id_member)
					FROM smf_log_polls|
	SELECT lp.id_poll, lp.id_member
				FROM smf_log_polls AS lp
					LEFT JOIN smf_members AS mem ON (mem.id_member = lp.id_member)
				WHERE lp.id_member BETWEEN _step_low AND _step_high
					AND lp.id_member > 0
					AND mem.id_member IS NULL|
	SELECT MAX(id_poll)
					FROM smf_log_polls|
	SELECT lp.id_poll, lp.id_member
				FROM smf_log_polls AS lp
					LEFT JOIN smf_polls AS p ON (p.id_poll = lp.id_poll)
				WHERE lp.id_poll BETWEEN _step_low AND _step_high
					AND p.id_poll IS NULL|
	SELECT MAX(id_report)
					FROM smf_log_reported|
	SELECT lr.id_report, lr.subject
				FROM smf_log_reported AS lr
					LEFT JOIN smf_log_reported_comments AS lrc ON (lrc.id_report = lr.id_report)
				WHERE lr.id_report BETWEEN _step_low AND _step_high
					AND lrc.id_report IS NULL|
	SELECT MAX(id_report)
					FROM smf_log_reported_comments|
	SELECT lrc.id_report, lrc.membername
				FROM smf_log_reported_comments AS lrc
					LEFT JOIN smf_log_reported AS lr ON (lr.id_report = lrc.id_report)
				WHERE lrc.id_report BETWEEN _step_low AND _step_high
					AND lr.id_report IS NULL|
	SELECT MAX(id_member)
					FROM smf_log_group_requests|
	SELECT lgr.id_member
				FROM smf_log_group_requests AS lgr
					LEFT JOIN smf_members AS mem ON (mem.id_member = lgr.id_member)
				WHERE lgr.id_member BETWEEN _step_low AND _step_high
					AND mem.id_member IS NULL
				GROUP BY lgr.id_member|
	SELECT MAX(id_group)
					FROM smf_log_group_requests|
	SELECT lgr.id_group
				FROM smf_log_group_requests AS lgr
					LEFT JOIN smf_membergroups AS mg ON (mg.id_group = lgr.id_group)
				WHERE lgr.id_group BETWEEN _step_low AND _step_high
					AND mg.id_group IS NULL
				GROUP BY lgr.id_group|
	SELECT id_cat
		FROM smf_categories
		WHERE name = _string
		LIMIT 1|
	SELECT id_board
		FROM smf_boards
		WHERE id_cat = _int
			AND name = _string
		LIMIT 1|
	SELECT mods.id_board, mods.id_member, mem.real_name
		FROM smf_moderators AS mods
			INNER JOIN smf_members AS mem ON (mem.id_member = mods.id_member)|
	SELECT id_group, group_name, online_color
		FROM smf_membergroups|
	SELECT b.id_board, b.name, b.num_posts, b.num_topics, b.count_posts, b.member_groups, b.override_theme, b.id_profile,
			c.name AS cat_name, IFNULL(par.name, _string) AS parent_name, IFNULL(th.value, _string) AS theme_name
		FROM smf_boards AS b
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
			LEFT JOIN smf_boards AS par ON (par.id_board = b.id_parent)
			LEFT JOIN smf_themes AS th ON (th.id_theme = b.id_theme AND th.variable = _string)|
	SELECT id_board, name, member_groups, id_profile
		FROM smf_boards|
	SELECT mg.id_group, mg.group_name, mg.online_color, mg.min_posts, mg.max_messages, mg.stars,
			CASE WHEN bp.permission IS NOT NULL OR mg.id_group = _int THEN 1 ELSE 0 END AS can_moderate
		FROM smf_membergroups AS mg
			LEFT JOIN smf_board_permissions AS bp ON (bp.id_group = mg.id_group AND bp.id_profile = _int AND bp.permission = _string)
		ORDER BY mg.min_posts, CASE WHEN mg.id_group < _int THEN mg.id_group ELSE 4 END, mg.group_name|
	SELECT id_board, name
		FROM smf_boards|
	SELECT mods.id_board, mods.id_member
		FROM smf_moderators AS mods|
	SELECT id_group, group_name, online_color
		FROM smf_membergroups|
	SELECT id_member, real_name, id_group, posts, last_login
		FROM smf_members
		WHERE id_member IN (_array_int)
		ORDER BY real_name|
	SELECT id_task, task, `next_time`, `time_offset`, `time_regularity`, `time_unit`
			FROM smf_scheduled_tasks
			WHERE disabled = _int
				AND `next_time` <= _int
			ORDER BY `next_time` ASC
			LIMIT 1|
	SELECT `next_time`
			FROM smf_scheduled_tasks
			WHERE disabled = _int
			ORDER BY `next_time` ASC
			LIMIT 1|
	SELECT aq.id_msg, aq.id_attach, aq.id_event, m.id_topic, m.id_board, m.subject, t.id_first_msg,
			b.id_profile
		FROM smf_approval_queue AS aq
			INNER JOIN smf_messages AS m ON (m.id_msg = aq.id_msg)
			INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)|
	SELECT id_group, id_profile, add_deny
		FROM smf_board_permissions
		WHERE permission = _string
			AND id_profile IN (_array_int)|
	SELECT id_member, id_board
			FROM smf_moderators|
	SELECT id_member, real_name, email_address, lngfile, id_group, additional_groups, mod_prefs
		FROM smf_members
		WHERE id_group IN (_array_int)
			OR FIND_IN_SET(_raw, additional_groups) != 0|
	SELECT id_member, warning
			FROM smf_members
			WHERE warning > _int|
	SELECT id_recipient, MAX(log_time) AS last_warning
				FROM smf_log_comments
				WHERE id_recipient IN (_array_int)
					AND comment_type = _string
				GROUP BY id_recipient|
	SELECT COUNT(*)
			FROM smf_log_online|
	SELECT ln.id_topic, COALESCE(t.id_board, ln.id_board) AS id_board, mem.email_address, mem.member_name, mem.notify_types,
			mem.lngfile, mem.id_member
		FROM smf_log_notify AS ln
			INNER JOIN smf_members AS mem ON (mem.id_member = ln.id_member)
			LEFT JOIN smf_topics AS t ON (ln.id_topic != _int AND t.id_topic = ln.id_topic)
		WHERE mem.notify_regularity = _int
			AND mem.is_activated = _int|
	SELECT id_board, name
		FROM smf_boards
		WHERE id_board IN (_array_int)|
	SELECT id_task, `next_time`, `time_offset`, `time_regularity`, `time_unit`
		FROM smf_scheduled_tasks
		WHERE disabled = _int
			|
	SELECT id_theme, variable, value
		FROM smf_themes
		WHERE id_member = _int
			AND id_theme IN (1, _int)|
	SELECT id_file, filename, path, parameters
		FROM smf_admin_info_files|
	SELECT id_member, real_name, lngfile, email_address
		FROM smf_members
		WHERE is_activated < 10
			AND MONTH(birthdate) = _int
			AND DAYOFMONTH(birthdate) = _int
			AND notify_announcements = _int
			AND YEAR(birthdate) > _int|
	SELECT id_report
				FROM smf_log_reported
				WHERE time_started < _int|
	SELECT id_subscribe, id_member
		FROM smf_log_subscribed
		WHERE status = _int
			AND end_time < _int|
	SELECT ls.id_sublog, m.id_member, m.member_name, m.email_address, m.lngfile, s.name, ls.end_time
		FROM smf_log_subscribed AS ls
			INNER JOIN smf_subscriptions AS s ON (s.id_subscribe = ls.id_subscribe)
			INNER JOIN smf_members AS m ON (m.id_member = ls.id_member)
		WHERE ls.status = _int
			AND ls.reminder_sent = _int
			AND s.reminder > _int
			AND ls.end_time < (_int + s.reminder * 86400)|
	SELECT b.id_cat, c.name AS cat_name, b.id_board, b.name, b.child_level
		FROM smf_boards AS b
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
		WHERE {query_see_board}|
	SELECT ms.subject
			FROM smf_topics AS t
				INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
				INNER JOIN smf_messages AS ms ON (ms.id_msg = t.id_first_msg)
			WHERE t.id_topic = _int
				AND {query_see_board}|
	SELECT id_member
			FROM smf_members
			WHERE _raw|
	SELECT b.id_board
			FROM smf_topics AS t
				INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
			WHERE t.id_topic = _int
				AND {query_see_board}|
	SELECT b.id_board
			FROM smf_boards AS b
			WHERE _raw|
	SELECT COUNT(*)
			FROM smf_boards|
	SELECT
				m.id_msg, m.subject, m.poster_name, m.poster_email, m.poster_time, m.id_member,
				m.icon, m.poster_ip, m.body, m.smileys_enabled, m.modified_time, m.modified_name,
				first_m.id_msg AS first_msg, first_m.subject AS first_subject, first_m.icon AS first_icon, first_m.poster_time AS first_poster_time,
				first_mem.id_member AS first_member_id, IFNULL(first_mem.real_name, first_m.poster_name) AS first_member_name,
				last_m.id_msg AS last_msg, last_m.poster_time AS last_poster_time, last_mem.id_member AS last_member_id,
				IFNULL(last_mem.real_name, last_m.poster_name) AS last_member_name, last_m.icon AS last_icon, last_m.subject AS last_subject,
				t.id_topic, t.is_sticky, t.locked, t.id_poll, t.num_replies, t.num_views,
				b.id_board, b.name AS board_name, c.id_cat, c.name AS cat_name
			FROM smf_messages AS m
				INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
				INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
				INNER JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
				INNER JOIN smf_messages AS first_m ON (first_m.id_msg = t.id_first_msg)
				INNER JOIN smf_messages AS last_m ON (last_m.id_msg = t.id_last_msg)
				LEFT JOIN smf_members AS first_mem ON (first_mem.id_member = first_m.id_member)
				LEFT JOIN smf_members AS last_mem ON (last_mem.id_member = first_m.id_member)
			WHERE m.id_msg IN (_array_int)|
	SELECT b.id_board, bp.add_deny
		FROM smf_board_permissions AS bp
			INNER JOIN smf_boards AS b ON (b.id_profile = bp.id_profile)
			LEFT JOIN smf_moderators AS mods ON (mods.id_board = b.id_board AND mods.id_member = _int)
		WHERE bp.id_group IN (_array_int, _int)
			AND bp.permission IN (_array_string)
			AND (mods.id_member IS NOT NULL OR bp.id_group != _int)|
	SELECT m.subject, t.approved
		FROM smf_topics AS t
			INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
		WHERE t.id_topic = _int
		LIMIT 1|
	SELECT email_address AS email, real_name AS name, id_member, hide_email
			FROM smf_members
			WHERE id_member = _int|
	SELECT IFNULL(mem.email_address, m.poster_email) AS email, IFNULL(mem.real_name, m.poster_name) AS name, IFNULL(mem.id_member, 0) AS id_member, hide_email
			FROM smf_messages AS m
				LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
			WHERE m.id_msg = _int|
	SELECT m.id_msg, m.id_member, t.id_member_started
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = _int)
		WHERE m.id_msg = _int
			AND m.id_topic = _int
		LIMIT 1|
	SELECT m.id_topic, m.id_board, m.subject, m.body, m.id_member AS id_poster, m.poster_name, mem.real_name
		FROM smf_messages AS m
			LEFT JOIN smf_members AS mem ON (m.id_member = mem.id_member)
		WHERE m.id_msg = _int
			AND m.id_topic = _int
		LIMIT 1|
	SELECT id_member, email_address, lngfile, mod_prefs
		FROM smf_members
		WHERE id_member IN (_array_int)
			AND notify_types != _int
		ORDER BY lngfile|
	SELECT id_report, ignore_all
			FROM smf_log_reported
			WHERE id_msg = _int
				AND (closed = _int OR ignore_all = _int)
			ORDER BY ignore_all DESC|
	SELECT id_member
		FROM smf_moderators
		WHERE id_board = _int|
	SELECT m.subject, t.num_replies, t.id_first_msg, t.approved
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = _int)
		WHERE m.id_msg = _int|
	SELECT id_msg
			FROM smf_messages
			WHERE id_topic = _int
				AND id_msg >= _int|
	SELECT id_msg
			FROM smf_messages
			WHERE id_topic = _int|
	SELECT id_msg
				FROM smf_messages
				WHERE id_topic = _int
					AND id_msg IN (_array_int)|
	SELECT id_msg
			FROM smf_messages
			WHERE id_topic = _int
				AND id_msg IN (_array_int)|
	SELECT m.subject, IFNULL(mem.real_name, m.poster_name) AS real_name, m.poster_time, m.body, m.id_msg, m.smileys_enabled
		FROM smf_messages AS m
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE m.id_topic = _int|
	SELECT m.subject, IFNULL(mem.real_name, m.poster_name) AS real_name,  m.poster_time, m.body, m.id_msg, m.smileys_enabled
			FROM smf_messages AS m
				LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
			WHERE m.id_topic = _int
				AND m.id_msg IN (_array_int)|
	SELECT id_board, approved
		FROM smf_topics
		WHERE id_topic = _int
		LIMIT 1|
	SELECT
			MIN(m.id_msg) AS myid_first_msg, MAX(m.id_msg) AS myid_last_msg, COUNT(*) AS message_count, m.approved
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = _int)
		WHERE m.id_msg NOT IN (_array_int)
			AND m.id_topic = _int
		GROUP BY m.approved
		ORDER BY m.approved DESC
		LIMIT 2|
	SELECT MIN(id_msg) AS myid_first_msg, MAX(id_msg) AS myid_last_msg, COUNT(*) AS message_count, approved
		FROM smf_messages
		WHERE id_msg IN (_array_int)
			AND id_topic = _int
		GROUP BY id_topic, approved
		ORDER BY approved DESC
		LIMIT 2|
	SELECT id_member, id_msg
		FROM smf_log_topics
		WHERE id_topic = _int|
	SELECT COUNT(*)
		FROM smf_topics AS t
		WHERE t.id_board = _int|
	SELECT m.subject
		FROM smf_topics AS t
			INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
		WHERE t.id_topic = _int
			AND t.id_board = _int|
	SELECT b.id_board, b.name AS board_name, c.name AS cat_name
		FROM smf_boards AS b
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)
		WHERE {query_see_board}|
	SELECT t.id_topic, m.subject, m.id_member, IFNULL(mem.real_name, m.poster_name) AS poster_name
		FROM smf_topics AS t
			INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE t.id_board = _int
			AND t.id_topic != _int|
	SELECT b.id_board
		FROM smf_boards AS b
		WHERE b.id_board IN (_array_int)
			AND {query_see_board}|
	SELECT approved, MIN(id_msg) AS first_msg, MAX(id_msg) AS last_msg, COUNT(*) AS message_count
		FROM smf_messages
		WHERE id_topic IN (_array_int)
		GROUP BY approved
		ORDER BY approved DESC|
	SELECT id_member
		FROM smf_messages
		WHERE id_msg IN (_int, _int)
		ORDER BY id_msg
		LIMIT 2|
	SELECT id_member, MIN(id_msg) AS new_id_msg
		FROM smf_log_topics
		WHERE id_topic IN (_array_int)
		GROUP BY id_member|
	SELECT id_member, MAX(sent) AS sent
			FROM smf_log_notify
			WHERE id_topic IN (_array_int)
			GROUP BY id_member|
	SELECT id_board
		FROM smf_topics
		WHERE id_topic = _int
		LIMIT 1|
	SELECT
			SUM(posts) AS posts, SUM(topics) AS topics, SUM(registers) AS registers,
			SUM(most_on) AS most_on, MIN(date) AS date, SUM(hits) AS hits
		FROM smf_log_activity|
	SELECT COUNT(*)
		FROM smf_log_online|
	SELECT COUNT(*)
		FROM smf_boards AS b
		WHERE b.redirect = _string|
	SELECT COUNT(*)
		FROM smf_categories AS c|
	SELECT COUNT(*) AS total_members, gender
			FROM smf_members
			GROUP BY gender|
	SELECT most_on
		FROM smf_log_activity
		WHERE date = _date
		LIMIT 1|
	SELECT id_member, real_name, posts
		FROM smf_members
		WHERE posts > _int
		ORDER BY posts DESC
		LIMIT 10|
	SELECT id_board, name, num_posts
		FROM smf_boards AS b
		WHERE {query_see_board}|
	SELECT id_topic
			FROM smf_topics
			WHERE num_replies != _int|
	SELECT id_topic
			FROM smf_topics
			WHERE num_views != _int
			ORDER BY num_views DESC
			LIMIT 100|
	SELECT id_member_started, COUNT(*) AS hits
			FROM smf_topics|
	SELECT id_member, real_name
		FROM smf_members
		WHERE id_member IN (_array_int)
		ORDER BY FIND_IN_SET(id_member, _string)
		LIMIT 10|
	SELECT id_member, real_name, total_time_logged_in
		FROM smf_members|
	SELECT
			YEAR(date) AS stats_year, MONTH(date) AS stats_month, SUM(hits) AS hits, SUM(registers) AS registers, SUM(topics) AS topics, SUM(posts) AS posts, MAX(most_on) AS most_on, COUNT(*) AS num_days
		FROM smf_log_activity
		GROUP BY stats_year, stats_month|
	SELECT id_group
		FROM smf_permissions
		WHERE permission = _string
			AND add_deny = _int
			AND id_group != _int|
	SELECT id_member, member_name, real_name, lngfile, email_address
		FROM smf_members
		WHERE (id_group IN (_array_int) OR FIND_IN_SET(_raw, additional_groups) != 0)
			AND notify_types != _int
		ORDER BY lngfile|
	SELECT real_name
		FROM smf_members
		WHERE real_name LIKE _string|
	SELECT member_name, email_address, lngfile
		FROM smf_members
		WHERE id_member = _int|
	SELECT id_group
			FROM smf_group_moderators
			WHERE id_member = _int|
	SELECT id_board
			FROM smf_moderators
			WHERE id_member = _int|
	SELECT MIN(id_topic)
		FROM smf_log_topics
		WHERE id_member = _int|
	SELECT lt.id_topic
		FROM smf_log_topics AS lt
			INNER JOIN smf_topics AS t /*!40000 USE INDEX (PRIMARY) */ ON (t.id_topic = lt.id_topic
				AND t.id_board IN (_array_int))
		WHERE lt.id_member = _int
			AND lt.id_topic >= _int|
	SELECT b.id_board
			FROM smf_boards AS b
			WHERE {query_see_board}|
	SELECT id_first_msg, id_last_msg
			FROM smf_topics
			WHERE id_topic = _int|
	SELECT MAX(id_msg)
					FROM smf_messages
					WHERE id_topic = _int
						AND id_msg >= _int
						AND id_msg < _int|
	SELECT b.id_board, b.id_parent
				FROM smf_boards AS b
				WHERE {query_see_board}
					AND b.child_level > _int
					AND b.id_board NOT IN (_array_int)
				ORDER BY child_level ASC
				|
	SELECT b.id_board
				FROM smf_boards AS b
				WHERE b.id_parent IN (_array_int)
					AND {query_see_board}|
	SELECT IFNULL(mem.id_member, 0)
		FROM smf_messages AS m
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE m.id_msg = _int
		LIMIT 1|
	SELECT id_profile
				FROM smf_boards
				WHERE id_board = _int
				LIMIT 1|
	SELECT id_topic
		FROM smf_topics
		WHERE id_board IN (_array_int)|
	SELECT id_board
		FROM smf_boards
		WHERE id_parent = _int|
	SELECT
			IFNULL(b.id_board, 0) AS id_board, b.id_parent, b.name AS board_name, b.description, b.child_level,
			b.board_order, b.count_posts, b.member_groups, b.id_theme, b.override_theme, b.id_profile, b.redirect,
			b.num_posts, b.num_topics, c.id_cat, c.name AS cat_name, c.cat_order, c.can_collapse
		FROM smf_categories AS c
			LEFT JOIN smf_boards AS b ON (b.id_cat = c.id_cat)
		ORDER BY c.cat_order, b.child_level, b.board_order|
	SELECT
			cal.id_event, cal.start_date, cal.end_date, cal.title, cal.id_member, cal.id_topic,
			cal.id_board, b.member_groups, t.id_first_msg, t.approved, b.id_board
		FROM smf_calendar AS cal
			LEFT JOIN smf_boards AS b ON (b.id_board = cal.id_board)
			LEFT JOIN smf_topics AS t ON (t.id_topic = cal.id_topic)
		WHERE cal.start_date <= _date
			AND cal.end_date >= _date|
	SELECT id_member_started
			FROM smf_topics
			WHERE id_topic = _int
			LIMIT 1|
	SELECT id_member
		FROM smf_calendar
		WHERE id_event = _int
		LIMIT 1|
	SELECT
			c.id_event, c.id_board, c.id_topic, MONTH(c.start_date) AS month,
			DAYOFMONTH(c.start_date) AS day, YEAR(c.start_date) AS year,
			(TO_DAYS(c.end_date) - TO_DAYS(c.start_date)) AS span, c.id_member, c.title,
			t.id_first_msg, t.id_member_started
		FROM smf_calendar AS c
			LEFT JOIN smf_topics AS t ON (t.id_topic = c.id_topic)
		WHERE c.id_event = _int|
	SELECT COUNT(*)
		FROM smf_calendar_holidays|
	SELECT id_cat, cat_order
			FROM smf_categories
			ORDER BY cat_order|
	SELECT id_board
			FROM smf_boards
			WHERE id_cat IN (_array_int)|
	SELECT mem.id_member, c.id_cat, IFNULL(cc.id_cat, 0) AS is_collapsed, c.can_collapse
			FROM smf_members AS mem
				INNER JOIN smf_categories AS c ON (c.id_cat IN (_array_int))
				LEFT JOIN smf_collapsed_categories AS cc ON (cc.id_cat = c.id_cat AND cc.id_member = mem.id_member)
			|
	SELECT id_action, extra
		FROM smf_log_actions
		WHERE action IN (_string, _string)|
	SELECT id_member, id_subscribe
			FROM smf_log_subscribed
			WHERE vendor_ref = _string
			LIMIT 1|
	SELECT ls.id_member, ls.id_subscribe
					FROM smf_log_subscribed AS ls
						INNER JOIN smf_members AS mem ON (mem.id_member = ls.id_member)
					WHERE mem.email_address = _string
					LIMIT 1|
	SELECT code, filename
					FROM smf_smileys
					WHERE filename IN (_array_string)|
	SELECT title, filename
				FROM smf_message_icons
				WHERE id_board IN (0, _int)|
	SELECT code, filename, description, smiley_row, hidden
					FROM smf_smileys
					WHERE hidden IN (0, 2)
					ORDER BY smiley_row, smiley_order|
	SELECT id_comment
				FROM smf_log_comments
				WHERE comment_type = _string|
	SELECT id_comment, recipient_name AS answer
				FROM smf_log_comments
				WHERE comment_type = _string
					AND id_comment IN (_array_int)|
	SELECT id_comment, body AS question
			FROM smf_log_comments
			WHERE comment_type = _string
				AND id_comment IN (_array_int)|
	SELECT id_member, real_name
		FROM smf_members
		WHERE real_name LIKE _string|
	SELECT group_name
		FROM smf_membergroups
		WHERE id_group IN (_array_int)|
	SELECT id_member, additional_groups
		FROM smf_members
		WHERE FIND_IN_SET(_raw, additional_groups) != 0|
	SELECT id_board, member_groups
		FROM smf_boards
		WHERE FIND_IN_SET(_raw, member_groups) != 0|
	SELECT id_group, group_name, min_posts
		FROM smf_membergroups
		WHERE id_group IN (_array_int)|
	SELECT id_member, id_group
		FROM smf_members AS members
		WHERE id_group IN (_array_int)
			AND id_member IN (_array_int)|
	SELECT id_group, group_name, min_posts
		FROM smf_membergroups
		WHERE id_group = _int|
	SELECT id_member, real_name
		FROM smf_members
		WHERE id_group = _int OR FIND_IN_SET(_int, additional_groups) != 0|
	SELECT id_group, group_name, online_color
		FROM smf_membergroups
		WHERE min_posts = _int
			AND hidden = _int
			AND id_group != _int
			AND online_color != _string
		ORDER BY group_name|
	SELECT id_group, group_name, min_posts, online_color, stars, 0 AS num_members
		FROM smf_membergroups
		WHERE min_posts |
	SELECT id_post_group AS id_group, COUNT(*) AS num_members
				FROM smf_members
				WHERE id_post_group IN (_array_int)
				GROUP BY id_post_group|
	SELECT id_group, COUNT(*) AS num_members
				FROM smf_members
				WHERE id_group IN (_array_int)
				GROUP BY id_group|
	SELECT mg.id_group, COUNT(*) AS num_members
				FROM smf_membergroups AS mg
					INNER JOIN smf_members AS mem ON (mem.additional_groups != _string
						AND mem.id_group != mg.id_group
						AND FIND_IN_SET(mg.id_group, mem.additional_groups) != 0)
				WHERE mg.id_group IN (_array_int)
				GROUP BY mg.id_group|
	SELECT
			lo.id_member, lo.log_time, lo.id_spider, mem.real_name, mem.member_name, mem.show_online,
			mg.online_color, mg.id_group, mg.group_name
		FROM smf_log_online AS lo
			LEFT JOIN smf_members AS mem ON (mem.id_member = lo.id_member)
			LEFT JOIN smf_membergroups AS mg ON (mg.id_group = CASE WHEN mem.id_group = _int THEN mem.id_post_group ELSE mem.id_group END)|
	SELECT most_on
			FROM smf_log_activity
			WHERE date = _date
			LIMIT 1|
	SELECT id_member, pm_ignore_list, buddy_list
		FROM smf_members
		WHERE FIND_IN_SET(_raw, pm_ignore_list) != 0 OR FIND_IN_SET(_raw, buddy_list) != 0|
	SELECT id_member
		FROM smf_members
		WHERE email_address = _string
			OR email_address = _string
		LIMIT 1|
	SELECT id_group
			FROM smf_membergroups
			WHERE min_posts != _int|
	SELECT id_group
		FROM smf_membergroups
		WHERE group_name LIKE _string
		LIMIT 1|
	SELECT id_group, add_deny
			FROM smf_permissions
			WHERE permission = _string|
	SELECT id_profile
				FROM smf_boards
				WHERE id_board = _int
				LIMIT 1|
	SELECT bp.id_group, bp.add_deny
			FROM smf_board_permissions AS bp
			WHERE bp.permission = _string
				AND bp.id_profile = _int|
	SELECT mem.id_member
		FROM smf_members AS mem|
	SELECT email_address, member_name
			FROM smf_members
			WHERE id_member = _int
			LIMIT 1|
	SELECT
			id_member, member_name, email_address, member_ip, member_ip2, is_activated
		FROM smf_members
		WHERE member_ip IN (_array_string)
			OR member_ip2 IN (_array_string)|
	SELECT
			m.poster_ip, mem.id_member, mem.member_name, mem.email_address, mem.is_activated
		FROM smf_messages AS m
			INNER JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE m.id_member != 0
			|
	SELECT RAND()|
	SELECT c.name AS cat_name, c.id_cat, b.id_board, b.name AS board_name, b.child_level
		FROM smf_boards AS b
			LEFT JOIN smf_categories AS c ON (c.id_cat = b.id_cat)|
	SELECT server_url, handle, secret, issued, expires, assoc_type
		FROM smf_openid_assoc
		WHERE server_url = _string|
	SELECT passwd, id_member, id_group, lngfile, is_activated, email_address, additional_groups, member_name, password_salt,
			openid_uri
		FROM smf_members
		WHERE openid_uri = _string|
	SELECT mem.id_member, mem.member_name
		FROM smf_members AS mem
		WHERE mem.openid_uri = _string|
	SELECT id_install, package_id, filename, name, version
		FROM smf_log_packages
		WHERE install_state != _int
		ORDER BY time_installed DESC|
	SELECT value
		FROM smf_themes
		WHERE id_member = _int
			AND variable = _string|
	SELECT COUNT(*), MAX(id_member)
				FROM smf_members
				WHERE is_activated = _int|
	SELECT real_name
				FROM smf_members
				WHERE id_member = _int
				LIMIT 1|
	SELECT COUNT(*)
					FROM smf_members
					WHERE is_activated IN (_array_int)|
	SELECT SUM(num_posts + unapproved_posts) AS total_messages, MAX(id_last_msg) AS max_msg_id
				FROM smf_boards
				WHERE redirect = _string|
	SELECT SUM(num_topics + unapproved_topics) AS total_topics
				FROM smf_boards|
	SELECT id_group, min_posts
				FROM smf_membergroups
				WHERE min_posts != _int|
	SELECT code, filename, description
					FROM smf_smileys|
	SELECT id_report
			FROM smf_log_reported
			WHERE _raw = _int
			LIMIT 1|
	SELECT file_hash
			FROM smf_attachments
			WHERE id_attach = _int|
	SELECT
			id_member, criteria, is_or
		FROM smf_pm_rules
		WHERE id_member IN (_array_int)
			AND delete_pm = _int|
	SELECT id_group, max_messages
			FROM smf_membergroups|
	SELECT id_group, add_deny
		FROM smf_permissions
		WHERE permission = _string|
	SELECT mf.subject, ml.body, ml.id_member, t.id_last_msg, t.id_topic,
			IFNULL(mem.real_name, ml.poster_name) AS poster_name
		FROM smf_topics AS t
			INNER JOIN smf_messages AS mf ON (mf.id_msg = t.id_first_msg)
			INNER JOIN smf_messages AS ml ON (ml.id_msg = t.id_last_msg)
			LEFT JOIN smf_members AS mem ON (mem.id_member = ml.id_member)
		WHERE t.id_topic IN (_array_int)
		LIMIT 1|
	SELECT
			mem.id_member, mem.email_address, mem.notify_regularity, mem.notify_types, mem.notify_send_body, mem.lngfile,
			ln.sent, mem.id_group, mem.additional_groups, b.member_groups, mem.id_post_group, t.id_member_started,
			ln.id_topic
		FROM smf_log_notify AS ln
			INNER JOIN smf_members AS mem ON (mem.id_member = ln.id_member)
			INNER JOIN smf_topics AS t ON (t.id_topic = ln.id_topic)
			INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
		WHERE ln.id_topic IN (_array_int)
			AND mem.notify_types < _int
			AND mem.notify_regularity < _int
			AND mem.is_activated = _int
			AND ln.id_member != _int|
	SELECT approved
			FROM smf_topics
			WHERE id_topic = _int
			LIMIT 1|
	SELECT member_name, email_address
				FROM smf_members
				WHERE id_member = _int
				LIMIT 1|
	SELECT id_attach
			FROM smf_attachments
			WHERE filename = _string
			LIMIT 1|
	SELECT body
				FROM smf_messages
				WHERE id_msg = _int|
	SELECT id_topic
			FROM smf_topics
			WHERE id_first_msg = _int
			LIMIT 1|
	SELECT m.id_msg, m.approved, m.id_topic, m.id_board, t.id_first_msg, t.id_last_msg,
			m.body, m.subject, IFNULL(mem.real_name, m.poster_name) AS poster_name, m.id_member,
			t.approved AS topic_approved, b.count_posts
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
			INNER JOIN smf_boards AS b ON (b.id_board = m.id_board)
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE m.id_msg IN (_array_int)
			AND m.approved = _int|
	SELECT id_topic, MAX(id_msg) AS id_last_msg
			FROM smf_messages
			WHERE id_topic IN (_array_int)
				AND approved = _int
			GROUP BY id_topic|
	SELECT id_msg
		FROM smf_messages
		WHERE id_topic IN (_array_int)
			AND approved = _int|
	SELECT
			mem.id_member, mem.email_address, mem.notify_regularity, mem.notify_types, mem.notify_send_body, mem.lngfile,
			ln.sent, mem.id_group, mem.additional_groups, b.member_groups, mem.id_post_group, t.id_member_started,
			ln.id_topic
		FROM smf_log_notify AS ln
			INNER JOIN smf_members AS mem ON (mem.id_member = ln.id_member)
			INNER JOIN smf_topics AS t ON (t.id_topic = ln.id_topic)
			INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
		WHERE ln.id_topic IN (_array_int)
			AND mem.is_activated = _int
			AND mem.notify_types < _int
			AND mem.notify_regularity < _int
		GROUP BY mem.id_member, ln.id_topic, mem.email_address, mem.notify_regularity, mem.notify_types, mem.notify_send_body, mem.lngfile, ln.sent, mem.id_group, mem.additional_groups, b.member_groups, mem.id_post_group, t.id_member_started
		ORDER BY mem.lngfile|
	SELECT id_board, MAX(id_last_msg) AS id_msg
			FROM smf_topics
			WHERE id_board IN (_array_int)
				AND approved = _int
			GROUP BY id_board|
	SELECT real_name
			FROM smf_members
			WHERE id_member = _int
			LIMIT 1|
	SELECT id_group
		FROM smf_permissions
		WHERE permission = _string
			AND add_deny = _int
			AND id_group != _int|
	SELECT id_member, lngfile, email_address
		FROM smf_members
		WHERE (id_group IN (_array_int) OR FIND_IN_SET(_raw, additional_groups) != 0)
			AND notify_types != _int
		ORDER BY lngfile|
	SELECT
			m.poster_time, m.subject, m.id_topic, m.id_member, m.id_msg,
			IFNULL(mem.real_name, m.poster_name) AS poster_name, t.id_board, b.name AS board_name,
			SUBSTRING(m.body, 1, 385) AS body, m.smileys_enabled
		FROM smf_messages AS m
			INNER JOIN smf_topics AS t ON (t.id_topic = m.id_topic)
			INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
			LEFT JOIN smf_members AS mem ON (mem.id_member = m.id_member)
		WHERE m.id_msg >= _int|
	SELECT id_theme, value AS name
			FROM smf_themes
			WHERE variable = _string
				AND id_member = _int
			ORDER BY id_theme|
	SELECT id_theme, variable, value
			FROM smf_themes
			WHERE variable IN (_string, _string, _string, _string, _string, _string)
				AND id_member = _int|
	SELECT id_theme, variable, value
		FROM smf_themes
		WHERE variable IN (_string, _string, _string, _string)
			AND id_member = _int|
	SELECT id_theme, variable, value
			FROM smf_themes
			WHERE variable IN (_string, _string)
				AND id_member = _int|
	SELECT id_theme, COUNT(*) AS value
			FROM smf_themes
			WHERE id_member = _int
			GROUP BY id_theme|
	SELECT col_name
			FROM smf_custom_fields|
	SELECT COUNT(DISTINCT id_member) AS value, id_theme
			FROM smf_themes
			WHERE id_member > _int
				|
	SELECT id_member, 1, SUBSTRING(_string, 1, 255), SUBSTRING(_string, 1, 65534)
					FROM smf_members|
	SELECT id_member, _int, SUBSTRING(_string, 1, 255), SUBSTRING(_string, 1, 65534)
					FROM smf_members|
	SELECT col_name
				FROM smf_custom_fields|
	SELECT variable, value
			FROM smf_themes
			WHERE id_theme IN (1, _int)
				AND id_member = _int|
	SELECT id_theme
			FROM smf_members
			WHERE id_member = _int
			LIMIT 1|
	SELECT id_theme, variable, value
			FROM smf_themes
			WHERE variable IN (_string, _string, _string, _string, _string)|
	SELECT id_theme, COUNT(*) AS the_count
		FROM smf_members
		GROUP BY id_theme
		ORDER BY id_theme DESC|
	SELECT id_theme, value
			FROM smf_themes
			WHERE variable = _string|
	SELECT value
			FROM smf_themes
			WHERE id_theme = _int
				AND id_member = _int
				AND variable = _string
			LIMIT 1|
	SELECT variable, value
			FROM smf_themes
			WHERE variable IN (_string, _string)
				AND id_member = _int
				AND id_theme = _int|
	SELECT MAX(id_theme)
			FROM smf_themes|
	SELECT id_theme, variable, value
			FROM smf_themes
			WHERE variable IN (_string, _string, _string, _string)
				AND id_member = _int|
	SELECT value, id_theme
		FROM smf_themes
		WHERE variable = _string
			AND id_theme = _int
		LIMIT 1|
	SELECT value
					FROM smf_themes
					WHERE variable = _string
						AND id_theme = _int
					LIMIT 1|
	SELECT th1.value, th1.id_theme, th2.value
		FROM smf_themes AS th1
			LEFT JOIN smf_themes AS th2 ON (th2.variable = _string AND th2.id_theme = _int)
		WHERE th1.variable = _string
			AND th1.id_theme = _int
		LIMIT 1|
	SELECT COUNT(*)
		FROM smf_log_online AS lo
			LEFT JOIN smf_members AS mem ON (lo.id_member = mem.id_member)|
	SELECT
			lo.log_time, lo.id_member, lo.url, INET_NTOA(lo.ip) AS ip, mem.real_name,
			lo.session, mg.online_color, IFNULL(mem.show_online, 1) AS show_online,
			lo.id_spider
		FROM smf_log_online AS lo
			LEFT JOIN smf_members AS mem ON (lo.id_member = mem.id_member)
			LEFT JOIN smf_membergroups AS mg ON (mg.id_group = CASE WHEN mem.id_group = _int THEN mem.id_post_group ELSE mem.id_group END)|
	SELECT t.id_topic, m.subject
			FROM smf_topics AS t
				INNER JOIN smf_boards AS b ON (b.id_board = t.id_board)
				INNER JOIN smf_messages AS m ON (m.id_msg = t.id_first_msg)
			WHERE {query_see_board}
				AND t.id_topic IN (_array_int);



insert_replace:
REPLACE INTO smf_log_boards (`id_msg`,`id_member`,`id_board`) VALUES (_int,_int,_int)|
	INSERT  INTO smf_attachments (`id_folder`,`id_msg`,`attachment_type`,`filename`,`file_hash`,`size`,`width`,`height`,`fileext`,`mime_type`) VALUES (_int,_int,_int,_string,_string,_int,_int,_int,_string,_string)|
	INSERT  INTO smf_log_errors (`id_member`,`log_time`,`ip`,`url`,`message`,`session`,`error_type`,`file`,`line`) VALUES (_int,_int,_string_16,_string_65534,_string_65534,_string,_string,_string_255,_int)|
	REPLACE INTO smf_log_karma (`action`,`id_target`,`id_executor`,`log_time`) VALUES (_int,_int,_int,_int)|
	INSERT IGNORE INTO smf_sessions (`session_id`,`data`,`last_update`) VALUES (_string,_string,_int)|
	INSERT  INTO smf_ban_groups (`name`,`ban_time`,`expire_time`,`cannot_access`,`cannot_register`,`cannot_post`,`cannot_login`,`reason`,`notes`) VALUES (_string_20,_int,_raw,_int,_int,_int,_int,_string_255,_string_65534)|
	INSERT  INTO smf_ban_items (`id_ban_group`,`ip_low1`,`ip_high1`,`ip_low2`,`ip_high2`,`ip_low3`,`ip_high3`,`ip_low4`,`ip_high4`,`hostname`,`email_address`,`id_member`) VALUES (_int,_int,_int,_int,_int,_int,_int,_int,_int,_string_255,_string_255,_int)|
	INSERT  INTO smf_calendar_holidays (`event_date`,`title`) VALUES (_date,_string_60)|
	INSERT  INTO smf_membergroups (`id_group`,`description`,`group_name`,`min_posts`,`stars`,`online_color`,`group_type`) VALUES (_int,_string,_string_80,_int,_string,_string,_int)|
	INSERT INTO smf_permissions (`id_group`,`permission`,`add_deny`) VALUES (_int,_string,_int)|
	INSERT INTO smf_board_permissions (`id_group`,`id_profile`,`permission`,`add_deny`) VALUES (_int,_int,_string,_int)|
	INSERT INTO smf_group_moderators (`id_group`,`id_member`) VALUES (_int,_int)|
	INSERT  INTO smf_log_actions (`log_time`,`id_log`,`id_member`,`ip`,`action`,`id_board`,`id_topic`,`id_msg`,`extra`) VALUES (_int,_int,_int,_string_16,_string,_int,_int,_int,_string_65534)|
	INSERT  INTO smf_subscriptions (`name`,`description`,`active`,`length`,`cost`,`id_group`,`add_groups`,`repeatable`,`allow_partial`,`email_complete`,`reminder`) VALUES (_string_60,_string_255,_int,_string_4,_string,_int,_string_40,_int,_int,_string,_int)|
	INSERT  INTO smf_log_subscribed (`id_subscribe`,`id_member`,`old_id_group`,`start_time`,`end_time`,`status`) VALUES (_int,_int,_int,_int,_int,_int)|
	INSERT  INTO smf_log_subscribed (`id_subscribe`,`id_member`,`old_id_group`,`start_time`,`end_time`,`status`,`pending_details`) VALUES (_int,_int,_int,_int,_int,_int,_string)|
	INSERT  INTO smf_permissions (`permission`,`id_group`,`add_deny`) VALUES (_string,_int,_int)|
	INSERT  INTO smf_board_permissions (`permission`,`id_group`,`id_profile`,`add_deny`) VALUES (_string,_int,_int,_int)|
	REPLACE INTO smf_permissions (`permission`,`id_group`,`add_deny`) VALUES (_string,_int,_int)|
	REPLACE INTO smf_board_permissions (`permission`,`id_group`,`id_profile`,`add_deny`) VALUES (_string,_int,_int,_int)|
	REPLACE INTO smf_permissions (`id_group`,`permission`,`add_deny`) VALUES (_int,_string,_int)|
	REPLACE INTO smf_board_permissions (`id_group`,`permission`,`add_deny`,`id_profile`) VALUES (_int,_string,_int,_int)|
	INSERT INTO smf_permissions (`id_group`,`permission`) VALUES (_int,_string)|
	INSERT INTO smf_board_permissions (`id_profile`,`id_group`,`permission`) VALUES (_int,_int,_string)|
	INSERT INTO smf_board_permissions (`id_profile`,`id_group`,`permission`) VALUES (_int,_int,_string)|
	INSERT INTO smf_board_permissions (`id_profile`,`id_group`,`permission`) VALUES (_int,_int,_string)|
	INSERT INTO smf_board_permissions (`id_profile`,`id_group`,`permission`) VALUES (_int,_int,_string)|
	INSERT INTO smf_permissions (`id_group`,`permission`,`add_deny`) VALUES (_int,_string,_int)|
	INSERT  INTO smf_permission_profiles (`profile_name`) VALUES (_string)|
	INSERT INTO smf_board_permissions (`id_profile`,`id_group`,`permission`,`add_deny`) VALUES (_int,_int,_string,_int)|
	INSERT INTO smf_permissions (`id_group`,`permission`,`add_deny`) VALUES (_int,_string,_int)|
	INSERT INTO smf_board_permissions (`id_group`,`id_profile`,`permission`,`add_deny`) VALUES (_int,_int,_string,_int)|
	INSERT  INTO smf_board_permissions (`id_profile`,`id_group`,`permission`,`add_deny`) VALUES (_int,_int,_string,_int)|
	INSERT  INTO smf_log_scheduled_tasks (`id_task`,`time_run`,`time_taken`) VALUES (_int,_int,_float)|
	INSERT INTO smf_spiders (`spider_name`,`user_agent`,`ip_info`) VALUES (_string,_string,_string)|
	INSERT IGNORE INTO smf_log_spider_stats (`id_spider`,`last_seen`,`stat_date`,`page_hits`) VALUES (_int,_int,_date,_int)|
	INSERT INTO smf_log_spider_hits (`id_spider`,`log_time`,`url`) VALUES (_int,_int,_string)|
	INSERT IGNORE INTO smf_log_spider_stats (`stat_date`,`id_spider`,`page_hits`,`last_seen`) VALUES (_date,_int,_int,_int)|
	INSERT  INTO smf_log_comments (`comment_type`,`body`,`recipient_name`) VALUES (_string,_string_65535,_string_80)|
	INSERT  INTO smf_custom_fields (`col_name`,`field_name`,`field_desc`,`field_type`,`field_length`,`field_options`,`show_reg`,`show_display`,`show_profile`,`private`,`active`,`default_value`,`can_search`,`bbc`,`mask`,`enclose`,`placement`) VALUES (_string,_string,_string,_string,_string,_string,_int,_int,_string,_int,_int,_string,_int,_int,_string,_string,_int)|
	INSERT  INTO smf_smileys (`code`,`filename`,`description`,`hidden`,`smiley_order`) VALUES (_string_30,_string_48,_string_80,_int,_int)|
	INSERT  INTO smf_smileys (`code`,`filename`,`description`,`smiley_row`,`smiley_order`) VALUES (_string_30,_string_48,_string_80,_int,_int)|
	REPLACE INTO smf_message_icons (`id_icon`,`id_board`,`title`,`filename`,`icon_order`) VALUES (_int,_int,_string_80,_string_80,_int)|
	REPLACE INTO smf_log_boards (`id_msg`,`id_member`,`id_board`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_topics (`id_msg`,`id_member`,`id_topic`) VALUES (_int,_int,_int)|
	INSERT  INTO smf_log_comments (`id_member`,`member_name`,`comment_type`,`recipient_name`,`body`,`log_time`) VALUES (_int,_string,_string,_string,_string,_int)|
	INSERT  INTO smf_log_comments (`id_member`,`member_name`,`comment_type`,`recipient_name`,`id_notice`,`body`,`log_time`) VALUES (_int,_string,_string,_string,_int,_string,_int)|
	INSERT  INTO smf_log_comments (`id_member`,`member_name`,`comment_type`,`id_recipient`,`recipient_name`,`body`,`log_time`) VALUES (_int,_string,_string,_int,_string_255,_string_65535,_int)|
	REPLACE INTO smf_log_topics (`id_topic`,`id_member`,`id_msg`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_topics (`id_topic`,`id_member`,`id_msg`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_boards (`id_board`,`id_member`,`id_msg`) VALUES (_int,_int,_int)|
	INSERT IGNORE INTO smf_log_notify (`id_member`,`id_topic`) VALUES (_int,_int)|
	INSERT IGNORE INTO smf_log_notify (`id_member`,`id_board`) VALUES (_int,_int)|
	INSERT  INTO smf_package_servers (`name`,`url`) VALUES (_string_255,_string_255)|
	INSERT  INTO smf_log_packages (`filename`,`name`,`package_id`,`version`,`id_member_installed`,`member_installed`,`time_installed`,`install_state`,`failed_steps`,`themes_installed`,`member_removed`,`db_changes`) VALUES (_string,_string,_string,_string,_int,_string,_int,_int,_string,_string,_int,_string)|
	INSERT  INTO smf_pm_rules (`id_member`,`rule_name`,`criteria`,`actions`,`delete_pm`,`is_or`) VALUES (_int,_string,_string,_string,_int,_int)|
	INSERT INTO smf_log_polls (`id_poll`,`id_member`,`id_choice`) VALUES (_int,_int,_int)|
	INSERT  INTO smf_polls (`question`,`hide_results`,`max_votes`,`expire_time`,`id_member`,`poster_name`,`change_vote`,`guest_vote`) VALUES (_string_255,_int,_int,_int,_int,_string_255,_int,_int)|
	INSERT  INTO smf_poll_choices (`id_poll`,`id_choice`,`label`,`votes`) VALUES (_int,_int,_string_255,_int)|
	INSERT  INTO smf_polls (`question`,`hide_results`,`max_votes`,`expire_time`,`id_member`,`poster_name`,`change_vote`,`guest_vote`) VALUES (_string_255,_int,_int,_int,_int,_string_255,_int,_int)|
	INSERT INTO smf_poll_choices (`id_poll`,`id_choice`,`label`) VALUES (_int,_int,_string_255)|
	INSERT IGNORE INTO smf_log_notify (`id_member`,`id_topic`,`id_board`) VALUES (_int,_int,_int)|
	INSERT  INTO smf_log_digest (`id_topic`,`id_msg`,`note_type`,`exclude`) VALUES (_int,_int,_string,_int)|
	INSERT  INTO smf_log_member_notices (`subject`,`body`) VALUES (_string_255,_string_65534)|
	INSERT  INTO smf_log_comments (`id_member`,`member_name`,`comment_type`,`id_recipient`,`recipient_name`,`log_time`,`id_notice`,`counter`,`body`) VALUES (_int,_string,_string,_int,_string_255,_int,_int,_int,_string_65534)|
	INSERT  INTO smf_log_subscribed (`id_subscribe`,`id_member`,`status`,`payments_pending`,`pending_details`,`start_time`,`vendor_ref`) VALUES (_int,_int,_int,_int,_string_65534,_int,_string_255)|
	REPLACE INTO smf_themes (`id_member`,`id_theme`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534)|
	REPLACE INTO smf_themes (`id_theme`,`variable`,`value`,`id_member`) VALUES (_int,_string_255,_string_65534,_int)|
	INSERT  INTO smf_log_actions (`action`,`id_log`,`log_time`,`id_member`,`ip`,`extra`) VALUES (_string,_int,_int,_int,_string_16,_string_65534)|
	INSERT  INTO smf_attachments (`id_member`,`attachment_type`,`filename`,`file_hash`,`fileext`,`size`,`width`,`height`,`mime_type`,`id_folder`) VALUES (_int,_int,_string,_string,_string,_int,_int,_int,_string,_int)|
	INSERT  INTO smf_log_group_requests (`id_member`,`id_group`,`time_applied`,`reason`) VALUES (_int,_int,_int,_string_65534)|
	INSERT  INTO smf_log_actions (`action`,`id_log`,`log_time`,`id_member`,`ip`,`extra`) VALUES (_string,_int,_int,_int,_string_16,_string_65534)|
	INSERT  INTO smf_topics (`id_board`,`id_member_started`,`id_member_updated`,`id_first_msg`,`id_last_msg`,`unapproved_posts`,`approved`,`id_previous_topic`) VALUES (_int,_int,_int,_int,_int,_int,_int,_int)|
	REPLACE INTO smf_log_topics (`id_topic`,`id_member`,`id_msg`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_boards (`id_board`,`id_member`,`id_msg`) VALUES (_int,_int,_int)|
	INSERT  INTO smf_categories (`name`,`cat_order`) VALUES (_string_255,_int)|
	INSERT  INTO smf_boards (`name`,`description`,`id_cat`,`member_groups`,`board_order`,`redirect`) VALUES (_string_255,_string_255,_int,_string,_int,_string)|
	INSERT  INTO smf_log_scheduled_tasks (`id_task`,`time_run`,`time_taken`) VALUES (_int,_int,_float)|
	REPLACE INTO smf_settings (`variable`,`value`) VALUES (_string,_string)|
	INSERT INTO smf_mail_queue (`recipient`,`body`,`subject`,`headers`,`send_html`) VALUES (_string,_string,_string,_string,_string)|
	INSERT  INTO smf_log_search_results (`id_search`,`id_topic`,`relevance`,`id_msg`,`num_matches`) VALUES (_int,_int,_int,_int,_int)|
	INSERT  INTO smf_log_search_results (`id_search`,`id_topic`,`relevance`,`id_msg`,`num_matches`) VALUES (_int,_int,_float,_int,_int)|
	INSERT  INTO smf_log_banned (`id_member`,`ip`,`email`,`log_time`) VALUES (_int,_string_16,_string,_int)|
	INSERT  INTO smf_log_reported (`id_msg`,`id_topic`,`id_board`,`id_member`,`membername`,`subject`,`body`,`time_started`,`time_updated`,`num_reports`,`closed`) VALUES (_int,_int,_int,_int,_string,_string,_string,_int,_int,_int,_int)|
	INSERT  INTO smf_log_reported_comments (`id_report`,`id_member`,`membername`,`comment`,`time_sent`) VALUES (_int,_int,_string,_string,_int)|
	INSERT  INTO smf_topics (`id_board`,`id_member_started`,`id_member_updated`,`id_first_msg`,`id_last_msg`,`num_replies`,`unapproved_posts`,`approved`,`is_sticky`) VALUES (_int,_int,_int,_int,_int,_int,_int,_int,_int)|
	INSERT IGNORE INTO smf_log_topics (`id_member`,`id_topic`,`id_msg`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_topics (`id_member`,`id_topic`,`id_msg`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_notify (`id_member`,`id_topic`,`id_board`,`sent`) VALUES (_int,_int,_int,_int)|
	REPLACE INTO smf_themes (`id_member`,`id_theme`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534)|
	REPLACE INTO smf_log_mark_read (`id_msg`,`id_member`,`id_board`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_boards (`id_msg`,`id_member`,`id_board`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_topics (`id_msg`,`id_member`,`id_topic`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_topics (`id_msg`,`id_member`,`id_topic`) VALUES (_int,_int,_int)|
	REPLACE INTO smf_log_boards (`id_msg`,`id_member`,`id_board`) VALUES (_int,_int,_int)|
	INSERT INTO smf_moderators (`id_board`,`id_member`) VALUES (_int,_int)|
	INSERT  INTO smf_boards (`id_cat`,`name`,`description`,`board_order`,`member_groups`,`redirect`) VALUES (_int,_string_255,_string,_int,_string,_string)|
	INSERT  INTO smf_calendar (`id_board`,`id_topic`,`title`,`id_member`,`start_date`,`end_date`) VALUES (_int,_int,_string_60,_int,_date,_date)|
	INSERT  INTO smf_categories (`name`) VALUES (_string_48)|
	REPLACE INTO smf_collapsed_categories (`id_cat`,`id_member`) VALUES (_int,_int)|
	INSERT  INTO smf_attachments (`id_member`,`attachment_type`,`filename`,`file_hash`,`fileext`,`size`,`id_folder`) VALUES (_int,_int,_string_255,_string_255,_string_8,_int,_int)|
	INSERT  INTO smf_log_actions (`log_time`,`id_log`,`id_member`,`ip`,`action`,`id_board`,`id_topic`,`id_msg`,`extra`) VALUES (_int,_int,_int,_string_16,_string,_int,_int,_int,_string_65534)|
	INSERT  INTO smf_log_actions (`log_time`,`id_log`,`id_member`,`ip`,`action`,`id_board`,`id_topic`,`id_msg`,`extra`) VALUES (_int,_int,_int,_string_16,_string,_int,_int,_int,_string_65534)|
	INSERT IGNORE INTO smf_log_activity (`date`,`most_on`) VALUES (_date,_int)|
	INSERT  INTO smf_log_actions (`log_time`,`id_log`,`id_member`,`ip`,`action`,`id_board`,`id_topic`,`id_msg`,`extra`) VALUES (_int,_int,_int,_string_16,_string,_int,_int,_int,_string_65534)|
	INSERT INTO smf_themes (`id_member`,`variable`,`value`) VALUES (_int,_string_255,_string_65534)|
	REPLACE INTO smf_themes (`id_member`,`id_theme`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534)|
	REPLACE INTO smf_openid_assoc (`server_url`,`handle`,`secret`,`issued`,`expires`,`assoc_type`) VALUES (_string,_string,_string,_int,_int,_string)|
	INSERT IGNORE INTO smf_log_search_subjects (`word`,`id_topic`) VALUES (_string,_int)|
	REPLACE INTO smf_settings (`variable`,`value`) VALUES (_string_255,_string_65534)|
	INSERT  INTO smf_log_actions (`log_time`,`id_log`,`id_member`,`ip`,`action`,`id_board`,`id_topic`,`id_msg`,`extra`) VALUES (_int,_int,_int,_string_16,_string,_int,_int,_int,_string_65534)|
	INSERT IGNORE INTO smf_log_activity (`date`) VALUES (_date)|
	REPLACE INTO smf_log_floodcontrol (`ip`,`log_time`,`log_type`) VALUES (_string_16,_int,_string)|
	INSERT  INTO smf_mail_queue (`time_sent`,`recipient`,`body`,`subject`,`headers`,`send_html`,`priority`,`private`) VALUES (_int,_string_255,_string_65534,_string_255,_string_65534,_int,_int,_int)|
	INSERT  INTO smf_mail_queue (`time_sent`,`recipient`,`body`,`subject`,`headers`,`send_html`,`priority`,`private`) VALUES (_int,_string_255,_string_65534,_string_255,_string_65534,_int,_int,_int)|
	INSERT  INTO smf_personal_messages (`id_pm_head`,`id_member_from`,`deleted_by_sender`,`from_name`,`msgtime`,`subject`,`body`) VALUES (_int,_int,_int,_string_255,_int,_string_255,_string_65534)|
	INSERT INTO smf_pm_recipients (`id_pm`,`id_member`,`bcc`,`deleted`,`is_new`) VALUES (_int,_int,_int,_int,_int)|
	INSERT  INTO smf_log_digest (`id_topic`,`id_msg`,`note_type`,`exclude`) VALUES (_int,_int,_string,_int)|
	INSERT  INTO smf_messages (`id_board`,`id_topic`,`id_member`,`subject`) VALUES (_int,_int,_int,_string_255)|
	INSERT  INTO smf_topics (`id_board`,`id_member_started`,`id_member_updated`,`id_first_msg`,`id_last_msg`,`locked`,`is_sticky`,`num_views`,`id_poll`,`unapproved_posts`,`approved`) VALUES (_int,_int,_int,_int,_int,_int,_int,_int,_int,_int,_int)|
	INSERT  INTO smf_approval_queue (`id_msg`) VALUES (_int)|
	INSERT IGNORE INTO smf_log_topics (`id_topic`,`id_member`,`id_msg`) VALUES (_int,_int,_int)|
	INSERT  INTO smf_attachments (`id_folder`,`id_msg`,`filename`,`file_hash`,`fileext`,`size`,`width`,`height`,`mime_type`,`approved`) VALUES (_int,_int,_string_255,_string_40,_string_8,_int,_int,_int,_string_20,_int)|
	INSERT  INTO smf_approval_queue (`id_attach`,`id_msg`) VALUES (_int,_int)|
	INSERT  INTO smf_attachments (`id_folder`,`id_msg`,`attachment_type`,`filename`,`file_hash`,`fileext`,`size`,`width`,`height`,`mime_type`,`approved`) VALUES (_int,_int,_int,_string_255,_string_40,_string_8,_int,_int,_int,_string_20,_int)|
	INSERT IGNORE INTO smf_log_topics (`id_topic`,`id_member`,`id_msg`) VALUES (_int,_int,_int)|
	INSERT IGNORE INTO smf_approval_queue (`id_msg`) VALUES (_int)|
	INSERT  INTO smf_log_digest (`id_topic`,`id_msg`,`note_type`,`exclude`) VALUES (_int,_int,_string,_int)|
	REPLACE INTO smf_themes (`id_theme`,`id_member`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534)|
	REPLACE INTO smf_themes (`id_member`,`id_theme`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534)|
	REPLACE INTO smf_themes (`id_member`,`id_theme`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534)|
	REPLACE INTO smf_themes (`id_theme`,`id_member`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534)|
	REPLACE INTO smf_themes (`id_theme`,`id_member`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534)|
	REPLACE INTO smf_themes (`id_theme`,`id_member`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534)|
	INSERT INTO smf_themes (`id_theme`,`variable`,`value`) VALUES (_int,_string_255,_string_65534)|
	REPLACE INTO smf_themes (`id_theme`,`id_member`,`variable`,`value`) VALUES (_int,_int,_string_255,_string_65534);



update:
UPDATE smf_topics
			SET num_views = num_views + 1
			WHERE id_topic = _int|
	UPDATE smf_log_notify
					SET sent = _int
					WHERE (id_topic = _int OR id_board = _int)
						AND id_member = _int|
	UPDATE LOW_PRIORITY smf_attachments
			SET downloads = downloads + 1
			WHERE id_attach = _int|
	UPDATE smf_attachments
								SET id_thumb = _int
								WHERE id_attach = _int|
	UPDATE smf_members
							SET id_group = _int, additional_groups = _string
							WHERE id_member = _int|
	UPDATE smf_log_karma
			SET action = _int, log_time = _int
			WHERE id_target = _int
				AND id_executor = _int|
	UPDATE smf_sessions
		SET `data` = _string, last_update = _int
		WHERE session_id = _string|
	UPDATE smf_topics
		SET locked = _int
		WHERE id_topic = _int|
	UPDATE smf_topics
		SET is_sticky = _int
		WHERE id_topic = _int|
	UPDATE smf_attachments
			SET attachment_type = _int
			WHERE id_attach IN (_array_int)|
	UPDATE smf_messages
					SET body = CONCAT(body, _string)
					WHERE id_msg IN (_array_int)|
	UPDATE smf_messages
			SET body = CONCAT(body, _string)
			WHERE id_msg IN (_array_int)|
	UPDATE smf_attachments
			SET id_thumb = _int
			WHERE id_attach IN (_array_int)|
	UPDATE smf_attachments
					SET id_thumb = _int
					WHERE id_attach IN (_array_int)|
	UPDATE smf_attachments
										SET id_folder = _int
										WHERE id_attach = _int|
	UPDATE smf_attachments
							SET size = _int
							WHERE id_attach = _int|
	UPDATE smf_attachments
					SET id_thumb = _int
					WHERE id_thumb IN (_array_int)|
	UPDATE smf_attachments
		SET approved = _int
		WHERE id_attach IN (_array_int)|
	UPDATE smf_attachments
						SET id_folder = _int
						WHERE id_folder = _int|
	UPDATE smf_ban_groups
				SET
					name = _string,
					reason = _string,
					notes = _string,
					expire_time = _raw,
					cannot_access = _int,
					cannot_post = _int,
					cannot_register = _int,
					cannot_login = _int
				WHERE id_ban_group = _int|
	UPDATE smf_calendar_holidays
					SET event_date = _date, title = _string
					WHERE id_holiday = _int|
	UPDATE smf_topics
					SET num_replies = _int
					WHERE id_topic = _int|
	UPDATE smf_topics
					SET unapproved_posts = _int
					WHERE id_topic = _int|
	UPDATE smf_boards
				SET num_posts = _int
				WHERE redirect = _string|
	UPDATE smf_boards
					SET num_posts = num_posts + _int
					WHERE id_board = _int|
	UPDATE smf_boards
				SET num_topics = _int|
	UPDATE smf_boards
					SET num_topics = num_topics + _int
					WHERE id_board = _int|
	UPDATE smf_boards
				SET unapproved_posts = _int|
	UPDATE smf_boards
					SET unapproved_posts = unapproved_posts + _int
					WHERE id_board = _int|
	UPDATE smf_boards
				SET unapproved_topics = _int|
	UPDATE smf_boards
					SET unapproved_topics = unapproved_topics + _int
					WHERE id_board = _int|
	UPDATE smf_messages
					SET id_board = _int
					WHERE id_msg IN (_array_int)|
	UPDATE smf_boards
					SET id_last_msg = _int, id_msg_updated = _int
					WHERE id_board = _int|
	UPDATE smf_membergroups
					SET
						online_color = _string,
						max_messages = _int,
						stars = _string
					WHERE id_group = _int|
	UPDATE smf_membergroups
					SET id_parent = _int
					WHERE id_group = _int|
	UPDATE smf_boards
				SET member_groups = CASE WHEN member_groups = _string THEN _string ELSE CONCAT(member_groups, _string) END
				WHERE id_board IN (_array_int)|
	UPDATE smf_membergroups
			SET group_name = _string, online_color = _string,
				max_messages = _int, min_posts = _int, stars = _string,
				description = _string, group_type = _int, hidden = _int,
				id_parent = _int
			WHERE id_group = _int|
	UPDATE smf_boards
					SET member_groups = _string
					WHERE id_board = _int|
	UPDATE smf_boards
					SET member_groups = CASE WHEN member_groups = _string THEN _string ELSE CONCAT(member_groups, _string) END
					WHERE id_board IN (_array_int)
						AND FIND_IN_SET(_int, member_groups) = 0|
	UPDATE smf_members
				SET id_group = _int
				WHERE id_group = _int|
	UPDATE smf_members
					SET id_group = _int
					WHERE id_group = _int|
	UPDATE smf_members
			SET validation_code = _string, is_activated = _int
			WHERE is_activated = _int|
	UPDATE smf_members
				SET validation_code = _string, is_activated = _int
				WHERE is_activated = _int
					|
	UPDATE smf_subscriptions
					SET name = SUBSTRING(_string, 1, 60), description = SUBSTRING(_string, 1, 255), active = _int,
					length = SUBSTRING(_string, 1, 4), cost = _string|
	UPDATE smf_log_subscribed
					SET start_time = _int, end_time = _int, status = _int
					WHERE id_sublog = _int|
	UPDATE smf_log_subscribed
							SET payments_pending = payments_pending - 1, pending_details = _string
							WHERE id_sublog = _int|
	UPDATE smf_members
			SET id_group = _int, additional_groups = _string
			WHERE id_member = _int
			LIMIT 1|
	UPDATE smf_log_subscribed
			SET end_time = _int, start_time = _int
			WHERE id_sublog = _int|
	UPDATE smf_members
		SET id_group = _int, additional_groups = _string
		WHERE id_member = _int|
	UPDATE smf_log_subscribed
			SET start_time = _int, end_time = _int, old_id_group = _int, status = _int,
				reminder_sent = _int
			WHERE id_sublog = _int|
	UPDATE smf_members
		SET id_group = _int, additional_groups = _string
		WHERE id_member = _int|
	UPDATE smf_log_subscribed
			SET status = _int
			WHERE id_member = _int
				AND id_subscribe = _int|
	UPDATE smf_boards
					SET id_profile = _int
					WHERE id_board IN (_array_int)|
	UPDATE smf_membergroups
				SET id_parent = _int
				WHERE id_parent IN (_array_int)|
	UPDATE smf_permission_profiles
						SET profile_name = _string
						WHERE id_profile = _int|
	UPDATE smf_scheduled_tasks
			SET disabled = CASE WHEN id_task IN (_array_int) THEN 0 ELSE 1 END|
	UPDATE smf_scheduled_tasks
			SET disabled = _int, `time_offset` = _int, `time_unit` = _string,
				`time_regularity` = _int
			WHERE id_task = _int|
	UPDATE smf_spiders
				SET spider_name = _string, user_agent = _string,
					ip_info = _string
				WHERE id_spider = _int|
	UPDATE smf_log_spider_stats
			SET last_seen = _int, page_hits = page_hits + 1
			WHERE id_spider = _int
				AND stat_date = _date|
	UPDATE smf_log_spider_hits
		SET processed = _int
		WHERE processed = _int|
	UPDATE smf_log_comments
							SET body = _string, recipient_name = _string
							WHERE comment_type = _string
								AND id_comment = _int|
	UPDATE smf_members
						SET signature = _string
						WHERE id_member = _int|
	UPDATE smf_themes
							SET value = _string
							WHERE variable = _string
								AND value = _string
								AND id_member > _int|
	UPDATE smf_custom_fields
				SET
					field_name = _string, field_desc = _string,
					field_type = _string, field_length = _int,
					field_options = _string, show_reg = _int,
					show_display = _int, show_profile = _string,
					private = _int, active = _int, default_value = _string,
					can_search = _int, bbc = _int, mask = _string,
					enclose = _string, placement = _int
				WHERE id_field = _int|
	UPDATE smf_smileys
						SET hidden = _int
						WHERE id_smiley IN (_array_int)|
	UPDATE smf_smileys
					SET
						code = _string,
						filename = _string,
						description = _string,
						hidden = _int
					WHERE id_smiley = _int|
	UPDATE smf_smileys
			SET smiley_order = smiley_order + 1
			WHERE hidden = _int
				AND smiley_row = _int
				AND smiley_order > _int|
	UPDATE smf_smileys
			SET
				smiley_order = _int + 1,
				smiley_row = _int,
				hidden = _int
			WHERE id_smiley = _int|
	UPDATE smf_smileys
					SET smiley_row = _int
					WHERE smiley_row = _int
						AND hidden = _int|
	UPDATE smf_smileys
						SET smiley_order = _int
						WHERE id_smiley = _int|
	UPDATE smf_boards
			SET num_posts = num_posts + 1
			WHERE id_board = _int|
	UPDATE smf_log_boards
				SET id_msg = _int
				WHERE id_member = _int
					AND id_board IN (_array_int)|
	UPDATE smf_log_notify
					SET sent = _int
					WHERE id_board = _int
						AND id_member = _int|
	UPDATE smf_topics
			SET is_sticky = CASE WHEN is_sticky = _int THEN 0 ELSE 1 END
			WHERE id_topic IN (_array_int)|
	UPDATE smf_log_comments
				SET id_recipient = _int, recipient_name = _string, body = _string
				WHERE id_comment = _int
					AND comment_type = _string
					AND (id_recipient = _int OR id_recipient = _int)|
	UPDATE smf_messages
					SET subject = _string
					WHERE id_topic = _int|
	UPDATE smf_messages
				SET subject = _string
				WHERE id_msg = _int|
	UPDATE smf_boards
			SET
				num_posts = CASE WHEN _int > num_posts THEN 0 ELSE num_posts - _int END,
				num_topics = CASE WHEN _int > num_topics THEN 0 ELSE num_topics - _int END,
				unapproved_posts = CASE WHEN _int > unapproved_posts THEN 0 ELSE unapproved_posts - _int END,
				unapproved_topics = CASE WHEN _int > unapproved_topics THEN 0 ELSE unapproved_topics - _int END
			WHERE id_board = _int|
	UPDATE smf_topics
		SET id_board = _int|
	UPDATE smf_topics
					SET id_first_msg = _int, id_last_msg = _int
					WHERE id_topic = _int|
	UPDATE smf_messages
		SET id_board = _int|
	UPDATE smf_log_reported
		SET id_board = _int
		WHERE id_topic IN (_array_int)|
	UPDATE smf_calendar
		SET id_board = _int
		WHERE id_topic IN (_array_int)|
	UPDATE smf_log_packages
					SET install_state = _int, member_removed = _string, id_member_removed = _int,
						`time_removed` = _int
					WHERE id_install = _int|
	UPDATE smf_log_packages
		SET install_state = _int|
	UPDATE smf_pm_recipients
			SET is_new = _int
			WHERE id_member = _int|
	UPDATE smf_pm_recipients
			SET is_read = is_read 
			WHERE id_pm = _int
				AND id_member = _int|
	UPDATE smf_pm_recipients
					SET labels = _string
					WHERE id_pm = _int
						AND id_member = _int|
	UPDATE smf_personal_messages
			SET deleted_by_sender = _int
			WHERE id_member_from IN (_array_int)
				AND deleted_by_sender = _int|
	UPDATE smf_pm_recipients
			SET deleted = _int
			WHERE id_member IN (_array_int)
				AND deleted = _int|
	UPDATE smf_pm_recipients
		SET is_read = is_read 
		WHERE id_member = _int
			AND NOT (is_read & 1 >= 1)|
	UPDATE smf_pm_recipients
					SET labels = _string
					WHERE id_pm = _int
						AND id_member = _int|
	UPDATE smf_pm_rules
						SET actions = _string
						WHERE id_rule = _int
							AND id_member = _int|
	UPDATE smf_pm_rules
				SET rule_name = _string, criteria = _string, actions = _string,
					delete_pm = _int, is_or = _int
				WHERE id_rule = _int
					AND id_member = _int|
	UPDATE smf_pm_recipients
				SET labels = _string
				WHERE id_pm = _int
					AND id_member = _int|
	UPDATE smf_poll_choices
				SET votes = votes - 1
				WHERE id_poll = _int
					AND id_choice IN (_array_int)
					AND votes > _int|
	UPDATE smf_poll_choices
		SET votes = votes + 1
		WHERE id_poll = _int
			AND id_choice IN (_array_int)|
	UPDATE smf_polls
			SET num_guest_voters = num_guest_voters + 1
			WHERE id_poll = _int|
	UPDATE smf_polls
		SET voting_locked = _int
		WHERE id_poll = _int|
	UPDATE smf_topics
			SET id_poll = _int
			WHERE id_topic = _int|
	UPDATE smf_poll_choices
				SET label = _string
				WHERE id_poll = _int
					AND id_choice = _int|
	UPDATE smf_polls
			SET num_guest_voters = _int, reset_poll = _int
			WHERE id_poll = _int|
	UPDATE smf_poll_choices
			SET votes = _int
			WHERE id_poll = _int|
	UPDATE smf_topics
		SET id_poll = _int
		WHERE id_topic = _int|
	UPDATE smf_calendar
				SET end_date = _date,
					start_date = _date,
					title = _string
				WHERE id_event = _int|
	UPDATE smf_log_boards
				SET id_msg = _int
				WHERE id_member = _int
					AND id_board IN (_array_int)|
	UPDATE smf_log_boards
			SET id_msg = _int
			WHERE id_member = _int
				AND id_board = _int|
	UPDATE smf_log_notify
		SET sent = _int
		WHERE id_board IN (_array_int)
			AND id_member != _int|
	UPDATE smf_messages
				SET subject = _string
				WHERE id_topic = _int
					AND id_msg != _int|
	UPDATE smf_log_subscribed
					SET payments_pending = payments_pending + 1, pending_details = _string
					WHERE id_sublog = _int
						AND id_member = _int|
	UPDATE smf_log_subscribed
					SET payments_pending = _int, pending_details = _string
					WHERE id_sublog = _int
						AND id_member = _int|
	UPDATE smf_topics
					SET id_previous_board = _int, locked = _int, is_sticky = _int
					WHERE id_topic = _int|
	UPDATE smf_messages
				SET icon = _string
				WHERE id_topic IN (_array_int)|
	UPDATE smf_log_reported
				SET closed = _int
				WHERE id_topic IN (_array_int)|
	UPDATE smf_boards
			SET
				num_posts = CASE WHEN _int > num_posts THEN 0 ELSE num_posts - _int END,
				num_topics = CASE WHEN _int > num_topics THEN 0 ELSE num_topics - _int END,
				unapproved_posts = CASE WHEN _int > unapproved_posts THEN 0 ELSE unapproved_posts - _int END,
				unapproved_topics = CASE WHEN _int > unapproved_topics THEN 0 ELSE unapproved_topics - _int END
			WHERE id_board = _int|
	UPDATE smf_log_reported
		SET closed = _int
		WHERE id_msg = _int|
	UPDATE smf_topics
			SET
				id_last_msg = _int,
				id_member_updated = _int|
	UPDATE smf_messages
				SET
					id_topic = _int,
					id_board = _int,
					icon = _string,
					approved = _int
				WHERE id_msg = _int|
	UPDATE smf_log_reported
				SET
					id_topic = _int,
					id_board = _int
				WHERE id_msg = _int|
	UPDATE smf_boards
				SET
					num_topics = num_topics + _int,
					num_posts = num_posts + 1|
	UPDATE smf_topics
					SET num_replies = num_replies + 1|
	UPDATE smf_messages
				SET icon = _string
				WHERE id_msg IN (_array_int)|
	UPDATE smf_messages
				SET icon = _string
				WHERE id_topic = _int|
	UPDATE smf_messages
		SET
			id_topic = _int,
			id_board = _int,
			icon = _string
		WHERE id_msg IN(_array_int)|
	UPDATE smf_boards
		SET
			num_posts = num_posts + _int,
			unapproved_posts = unapproved_posts + _int
		WHERE id_board = _int|
	UPDATE smf_topics
			SET
				id_first_msg = _int,
				id_last_msg = _int,
				num_replies = _int,
				unapproved_posts = _int
			WHERE id_topic = _int|
	UPDATE smf_boards
			SET
				num_posts = num_posts + _int,
				unapproved_posts = unapproved_posts + _int
			WHERE id_board = _int|
	UPDATE smf_topics
		SET
			id_first_msg = _int,
			id_last_msg = _int,
			num_replies = _int,
			unapproved_posts = _int
		WHERE id_topic = _int|
	UPDATE smf_topics
				SET id_topic = NULL
				WHERE id_topic = 0|
	UPDATE smf_messages
				SET id_msg = NULL
				WHERE id_msg = 0|
	UPDATE smf_scheduled_tasks
				SET `next_time` = _int
				WHERE id_task = _int
					AND `next_time` = _int|
	UPDATE smf_members
						SET warning = _int
						WHERE id_member = _int|
	UPDATE smf_log_digest
			SET daily = _int
			WHERE daily = _int|
	UPDATE smf_log_digest
			SET daily = _int
			WHERE daily = _int|
	UPDATE smf_log_notify
		SET sent = _int
		WHERE id_member IN (_array_int)|
	UPDATE smf_settings
			SET value = _string
			WHERE variable = _string
				AND value = _string|
	UPDATE smf_settings
			SET value = _string
			WHERE variable = _string
				AND value = _string|
	UPDATE smf_settings
				SET value = _string
				WHERE variable = _string
					AND value = _string|
	UPDATE smf_settings
			SET value = _string
			WHERE variable = _string|
	UPDATE smf_scheduled_tasks
			SET `next_time` = _int
			WHERE id_task = _int|
	UPDATE smf_admin_info_files
			SET `data` = SUBSTRING(_string, 1, 65534)
			WHERE id_file = _int|
	UPDATE smf_log_subscribed
			SET reminder_sent = _int
			WHERE id_sublog IN (_array_int)|
	UPDATE smf_ban_items
			SET hits = hits + 1
			WHERE id_ban IN (_array_int)|
	UPDATE smf_log_reported
				SET num_reports = num_reports + 1, `time_updated` = _int
				WHERE id_report = _int|
	UPDATE smf_messages
			SET
				id_topic = _int,
				subject = CASE WHEN id_msg = _int THEN _string ELSE _string END
			WHERE id_msg IN (_array_int)|
	UPDATE smf_log_reported
		SET id_topic = _int
		WHERE id_msg IN (_array_int)|
	UPDATE smf_topics
		SET
			num_replies = _int,
			id_first_msg = _int,
			id_last_msg = _int,
			id_member_started = _int,
			id_member_updated = _int,
			unapproved_posts = _int
		WHERE id_topic = _int|
	UPDATE smf_topics
		SET
			id_first_msg = _int,
			id_last_msg = _int
		WHERE id_topic = _int|
	UPDATE smf_messages
			SET approved = _int
			WHERE id_msg = _int
				AND id_topic = _int|
	UPDATE smf_topics
		SET
			id_board = _int,
			id_member_started = _int,
			id_member_updated = _int,
			id_first_msg = _int,
			id_last_msg = _int,
			id_poll = _int,
			num_replies = _int,
			unapproved_posts = _int,
			num_views = _int,
			is_sticky = _int,
			approved = _int
		WHERE id_topic = _int|
	UPDATE smf_messages
		SET
			id_topic = _int,
			id_board = _int|
	UPDATE smf_log_reported
		SET
			id_topic = _int,
			id_board = _int
		WHERE id_topic IN (_array_int)|
	UPDATE smf_messages
		SET subject = _string
		WHERE id_msg = _int|
	UPDATE smf_calendar
		SET
			id_topic = _int,
			id_board = _int
		WHERE id_topic IN (_array_int)|
	UPDATE smf_boards
			SET
				num_topics = CASE WHEN _int > num_topics THEN 0 ELSE num_topics - _int END,
				unapproved_topics = CASE WHEN _int > unapproved_topics THEN 0 ELSE unapproved_topics - _int END,
				num_posts = CASE WHEN _int > num_posts THEN 0 ELSE num_posts - _int END,
				unapproved_posts = CASE WHEN _int > unapproved_posts THEN 0 ELSE unapproved_posts - _int END
			WHERE id_board = _int|
	UPDATE smf_boards
			SET board_order = board_order + _int
			WHERE board_order > _int
				AND id_board != _int|
	UPDATE smf_boards
				SET id_profile = _int
				WHERE id_board = _int|
	UPDATE smf_boards
					SET board_order = _int
					WHERE id_board = _int|
	UPDATE smf_boards
		SET id_parent = _int, child_level = _int
		WHERE id_parent = _int|
	UPDATE smf_boards
						SET child_level = _int
						WHERE id_board = _int|
	UPDATE smf_calendar
		SET
			start_date = _date,
			end_date = _date,
			title = SUBSTRING(_string, 1, 60),
			id_board = _int,
			id_topic = _int
		WHERE id_event = _int|
	UPDATE smf_categories
					SET cat_order = _int
					WHERE id_cat = _int|
	UPDATE smf_boards
			SET id_cat = _int
			WHERE id_cat IN (_array_int)|
	UPDATE smf_log_actions
				SET extra = _string
				WHERE id_action = _int|
	UPDATE smf_log_subscribed
				SET vendor_ref = _string
				WHERE id_sublog = _int|
	UPDATE smf_attachments
				SET size = _int, width = _int, height = _int,
					mime_type = _string
				WHERE id_attach = _int|
	UPDATE smf_members
		SET id_group = _int
		WHERE id_group IN (_array_int)|
	UPDATE smf_membergroups
		SET id_parent = _int
		WHERE id_parent IN (_array_int)|
	UPDATE smf_boards
			SET member_groups = _string
			WHERE id_board IN (_array_int)|
	UPDATE smf_members
			SET
				id_group = _int,
				additional_groups = _string
			WHERE id_member IN (_array_int)|
	UPDATE smf_members
		SET id_group = _int
		WHERE id_group IN (_array_int)
			AND id_member IN (_array_int)|
	UPDATE smf_members
			SET additional_groups = _string
			WHERE id_member IN (_array_int)|
	UPDATE smf_members
			SET additional_groups = CASE WHEN additional_groups = _string THEN _string ELSE CONCAT(additional_groups, _string) END
			WHERE id_member IN (_array_int)
				AND id_group != _int
				AND FIND_IN_SET(_int, additional_groups) = 0|
	UPDATE smf_members
			SET id_group = _int
			WHERE id_member IN (_array_int)|
	UPDATE smf_members
			SET
				additional_groups = CASE WHEN id_group = _int THEN additional_groups
					WHEN additional_groups = _string THEN _string
					ELSE CONCAT(additional_groups, _string) END,
				id_group = CASE WHEN id_group = _int THEN _int ELSE id_group END
			WHERE id_member IN (_array_int)
				AND id_group != _int
				AND FIND_IN_SET(_int, additional_groups) = 0|
	UPDATE smf_messages
		SET id_member = _int, poster_email = _string
		WHERE id_member IN (_array_int)|
	UPDATE smf_polls
		SET id_member = _int
		WHERE id_member IN (_array_int)|
	UPDATE smf_topics
		SET id_member_started = _int
		WHERE id_member_started IN (_array_int)|
	UPDATE smf_topics
		SET id_member_updated = _int
		WHERE id_member_updated IN (_array_int)|
	UPDATE smf_log_actions
		SET id_member = _int
		WHERE id_member IN (_array_int)|
	UPDATE smf_log_banned
		SET id_member = _int
		WHERE id_member IN (_array_int)|
	UPDATE smf_log_errors
		SET id_member = _int
		WHERE id_member IN (_array_int)|
	UPDATE smf_log_polls
		SET id_member = _int
		WHERE id_member IN (_array_int)|
	UPDATE smf_personal_messages
		SET id_member_from = _int
		WHERE id_member_from IN (_array_int)|
	UPDATE smf_members
			SET
				pm_ignore_list = _string,
				buddy_list = _string
			WHERE id_member = _int|
	UPDATE smf_log_packages
			SET install_state = _int|
	UPDATE smf_log_online
			SET log_time = _int, ip = IFNULL(INET_ATON(_string), 0), url = _string
			WHERE session = _string|
	UPDATE smf_settings
			SET value = _string
			WHERE variable = _string
				AND value = _string|
	UPDATE smf_personal_messages
				SET id_pm_head = _int
				WHERE id_pm = _int|
	UPDATE smf_log_notify
			SET sent = _int
			WHERE id_topic IN (_array_int)
				AND id_member != _int|
	UPDATE smf_log_notify
					SET sent = _int
					WHERE id_topic = _int
						AND id_member = _int|
	UPDATE smf_attachments
			SET id_msg = _int
			WHERE id_attach IN (_array_int)|
	UPDATE smf_messages
			SET id_topic = _int
			WHERE id_msg = _int|
	UPDATE smf_messages
		SET id_msg_modified = _int
		WHERE id_msg = _int|
	UPDATE smf_boards
			SET num_posts = num_posts + 1|
	UPDATE smf_boards
			SET unapproved_posts = unapproved_posts + 1|
	UPDATE smf_log_topics
				SET id_msg = _int
				WHERE id_member = _int
					AND id_topic = _int|
	UPDATE smf_attachments
				SET
					width = _int,
					height = _int,
					mime_type = _string
				WHERE id_attach = _int|
	UPDATE smf_attachments
					SET id_thumb = _int
					WHERE id_attach = _int|
	UPDATE smf_topics
			SET
				is_sticky = _raw,
				locked = _raw,
				id_poll = _raw
			WHERE id_topic = _int|
	UPDATE smf_log_topics
			SET id_msg = _int
			WHERE id_member = _int
				AND id_topic = _int|
	UPDATE smf_messages
		SET approved = _int
		WHERE id_msg IN (_array_int)|
	UPDATE smf_topics
			SET approved = _int, unapproved_posts = unapproved_posts + _int,
				num_replies = num_replies + _int, id_last_msg = _int
			WHERE id_topic = _int|
	UPDATE smf_boards
			SET num_posts = num_posts + _int, unapproved_posts = unapproved_posts + _int,
				num_topics = num_topics + _int, unapproved_topics = unapproved_topics + _int
			WHERE id_board = _int|
	UPDATE smf_log_notify
			SET sent = _int
			WHERE id_topic IN (_array_int)
				AND id_member != _int|
	UPDATE smf_boards
			SET id_msg_updated = _int
			WHERE id_board IN (_array_int)
				AND id_msg_updated < _int|
	UPDATE smf_boards
			SET id_last_msg = _int, id_msg_updated = _int
			WHERE id_board IN (_array_int)|
	UPDATE smf_members
		SET id_theme = _int
		WHERE id_theme = _int|
	UPDATE smf_boards
		SET id_theme = _int
		WHERE id_theme = _int;



delete:
DELETE FROM smf_settings
					WHERE variable = _string
						OR variable = _string|
	DELETE FROM smf_log_group_requests
				WHERE id_request IN (_array_int)|
	DELETE FROM smf_log_karma
		WHERE _int - log_time > _int|
	DELETE FROM smf_sessions
		WHERE session_id = _string|
	DELETE FROM smf_sessions
		WHERE last_update < _int|
	DELETE FROM smf_log_online
		WHERE session = _string|
	DELETE FROM smf_log_online
			WHERE id_member = _int|
	DELETE FROM smf_attachments
			WHERE id_attach IN (_array_int)|
	DELETE FROM smf_attachments
					WHERE id_attach IN (_array_int)
						AND attachment_type = _int|
	DELETE FROM smf_attachments
					WHERE id_attach IN (_array_int)|
	DELETE FROM smf_attachments
					WHERE id_attach IN (_array_int)
						AND id_member != _int
						AND id_msg = _int|
	DELETE FROM smf_attachments
					WHERE id_attach IN (_array_int)
						AND id_member = _int
						AND id_msg != _int|
	DELETE FROM smf_approval_queue
		WHERE id_attach IN (_array_int)|
	DELETE FROM smf_ban_groups
			WHERE id_ban_group IN (_array_int)|
	DELETE FROM smf_ban_items
			WHERE id_ban_group IN (_array_int)|
	DELETE FROM smf_ban_items
			WHERE id_ban IN (_array_int)
				AND id_ban_group = _int|
	DELETE FROM smf_ban_items
						WHERE id_ban = _int|
	DELETE FROM smf_ban_items
			WHERE id_ban IN (_array_int)|
	DELETE FROM smf_log_banned
				WHERE id_ban_log IN (_array_int)|
	DELETE FROM smf_log_online
			WHERE id_member IN (_array_int)|
	DELETE FROM smf_calendar_holidays
				WHERE id_holiday = _int|
	DELETE FROM smf_log_errors
			WHERE id_error IN (_array_int)|
	DELETE FROM smf_mail_queue
			WHERE id_mail IN (_array_int)|
	DELETE FROM smf_log_online|
	DELETE FROM smf_log_banned|
	DELETE FROM smf_log_floodcontrol|
	DELETE FROM smf_log_karma|
	DELETE FROM smf_group_moderators
			WHERE id_group = _int|
	DELETE FROM smf_subscriptions
			WHERE id_subscribe = _int|
	DELETE FROM smf_log_subscribed
			WHERE id_member = _int|
	DELETE FROM smf_log_subscribed
			WHERE id_member = _int
				AND id_subscribe = _int|
	DELETE FROM smf_permissions
				WHERE id_group IN (_array_int)
					|
	DELETE FROM smf_board_permissions
			WHERE id_group IN (_array_int)
				AND id_profile = _int|
	DELETE FROM smf_permissions
					WHERE id_group IN (_array_int)
						AND permission = _string
						|
	DELETE FROM smf_board_permissions
					WHERE id_group IN (_array_int)
						AND id_profile = _int
						AND permission = _string|
	DELETE FROM smf_permissions
			WHERE id_group = _int|
	DELETE FROM smf_permissions
			WHERE id_group = _int
			|
	DELETE FROM smf_board_permissions
		WHERE id_group = _int
			AND id_profile = _int|
	DELETE FROM smf_permissions
				WHERE add_deny = _int|
	DELETE FROM smf_board_permissions
				WHERE add_deny = _int|
	DELETE FROM smf_permissions
				WHERE id_group IN (_array_int)|
	DELETE FROM smf_board_permissions
				WHERE id_group IN (_array_int)|
	DELETE FROM smf_permissions
			WHERE id_group = _int
			|
	DELETE FROM smf_board_permissions
			WHERE id_group = _int
				AND id_profile = _int|
	DELETE FROM smf_board_permissions
				WHERE id_group = _int
					AND id_profile = _int|
	DELETE FROM smf_board_permissions
			WHERE id_profile = _int|
	DELETE FROM smf_permissions
		WHERE permission IN (_array_string)
		|
	DELETE FROM smf_permission_profiles
			WHERE id_profile IN (_array_int)|
	DELETE FROM smf_permissions
			WHERE id_group IN (_array_int)|
	DELETE FROM smf_board_permissions
			WHERE id_group IN (_array_int)
				|
	DELETE FROM smf_board_permissions
			WHERE id_profile = _int
				AND permission IN (_array_string)
				AND id_group IN (_array_int)|
	DELETE FROM smf_spiders
			WHERE id_spider IN (_array_int)|
	DELETE FROM smf_log_spider_hits
			WHERE id_spider IN (_array_int)|
	DELETE FROM smf_log_spider_stats
			WHERE id_spider IN (_array_int)|
	DELETE FROM smf_log_spider_hits
			WHERE log_time < _int|
	DELETE FROM smf_settings
			WHERE variable = _string|
	DELETE FROM smf_log_comments
							WHERE comment_type = _string
								AND id_comment = _int|
	DELETE FROM smf_themes
					WHERE variable = _string
						AND id_member > _int|
	DELETE FROM smf_themes
					WHERE variable = _string
						AND value NOT IN (_array_string)
						AND id_member > _int|
	DELETE FROM smf_themes
			WHERE variable = _string
				AND id_member > _int|
	DELETE FROM smf_custom_fields
			WHERE id_field = _int|
	DELETE FROM smf_smileys
					WHERE id_smiley IN (_array_int)|
	DELETE FROM smf_smileys
					WHERE id_smiley = _int|
	DELETE FROM smf_message_icons
				WHERE id_icon IN (_array_int)|
	DELETE FROM smf_log_comments
			WHERE id_comment = _int
				AND comment_type = _string|
	DELETE FROM smf_log_comments
			WHERE id_comment IN (_array_int)
				AND comment_type = _string
				AND (id_recipient = _int OR id_recipient = _int)|
	DELETE FROM smf_log_actions
			WHERE id_log = _int
				AND log_time < _int|
	DELETE FROM smf_log_actions
			WHERE id_log = _int
				AND id_action IN (_array_string)
				AND log_time < _int|
	DELETE FROM smf_approval_queue
				WHERE id_msg IN (_array_int)
					AND id_attach = _int|
	DELETE FROM smf_log_notify
			WHERE id_member = _int
				AND id_topic = _int|
	DELETE FROM smf_log_notify
			WHERE id_member = _int
				AND id_board = _int|
	DELETE FROM smf_package_servers
		WHERE id_server = _int|
	DELETE FROM smf_personal_messages
			WHERE id_pm IN (_array_int)|
	DELETE FROM smf_pm_recipients
			WHERE id_pm IN (_array_int)|
	DELETE FROM smf_pm_rules
					WHERE id_rule IN (_array_int)
							AND id_member = _int|
	DELETE FROM smf_pm_rules
				WHERE id_rule IN (_array_int)
					AND id_member = _int|
	DELETE FROM smf_log_polls
				WHERE id_member = _int
					AND id_poll = _int|
	DELETE FROM smf_log_polls
			WHERE id_poll = _int
				AND id_choice IN (_array_int)|
	DELETE FROM smf_poll_choices
			WHERE id_poll = _int
				AND id_choice IN (_array_int)|
	DELETE FROM smf_log_polls
			WHERE id_poll = _int|
	DELETE FROM smf_log_polls
		WHERE id_poll = _int|
	DELETE FROM smf_poll_choices
		WHERE id_poll = _int|
	DELETE FROM smf_polls
		WHERE id_poll = _int|
	DELETE FROM smf_calendar
				WHERE id_event = _int|
	DELETE FROM smf_log_notify
			WHERE id_member = _int
				AND id_topic = _int|
	DELETE FROM smf_themes
				WHERE id_theme != _int
					AND variable IN (_array_string)
					AND id_member = _int|
	DELETE FROM smf_log_notify
			WHERE id_board IN (_array_int)
				AND id_member = _int|
	DELETE FROM smf_log_notify
			WHERE id_topic IN (_array_int)
				AND id_member = _int|
	DELETE FROM smf_log_online
		WHERE id_member = _int|
	DELETE FROM smf_polls
			WHERE id_poll IN (_array_int)|
	DELETE FROM smf_poll_choices
			WHERE id_poll IN (_array_int)|
	DELETE FROM smf_log_polls
			WHERE id_poll IN (_array_int)|
	DELETE FROM smf_messages
		WHERE id_topic IN (_array_int)|
	DELETE FROM smf_calendar
		WHERE id_topic IN (_array_int)|
	DELETE FROM smf_log_topics
		WHERE id_topic IN (_array_int)|
	DELETE FROM smf_log_notify
		WHERE id_topic IN (_array_int)|
	DELETE FROM smf_topics
		WHERE id_topic IN (_array_int)|
	DELETE FROM smf_log_search_subjects
		WHERE id_topic IN (_array_int)|
	DELETE FROM smf_approval_queue
				WHERE id_msg = _int
					AND id_attach = _int|
	DELETE FROM smf_messages
			WHERE id_msg = _int|
	DELETE FROM smf_approval_queue|
	DELETE FROM smf_settings
			WHERE variable = _string|
	DELETE FROM smf_settings
			WHERE variable = _string|
	DELETE FROM smf_log_digest
			WHERE daily != _int|
	DELETE FROM smf_log_digest
			WHERE daily = _int|
	DELETE FROM smf_mail_queue
			WHERE id_mail IN (_array_int)|
	DELETE FROM smf_settings
		WHERE variable IN (_array_string)
			AND (value = _string OR value = _string)|
	DELETE FROM smf_settings
		WHERE variable IN (_array_string)|
	DELETE FROM smf_log_errors
				WHERE log_time < _int|
	DELETE FROM smf_log_actions
				WHERE log_time < _int
					AND id_log = _int|
	DELETE FROM smf_log_banned
				WHERE log_time < _int|
	DELETE FROM smf_log_reported
					WHERE id_report IN (_array_int)|
	DELETE FROM smf_log_reported_comments
					WHERE id_report IN (_array_int)|
	DELETE FROM smf_log_scheduled_tasks
				WHERE time_run < _int|
	DELETE FROM smf_log_spider_hits
				WHERE log_time < _int|
	DELETE FROM smf_log_subscribed
		WHERE end_time = _int
			AND status = _int
			AND start_time < _int
			AND payments_pending < _int|
	DELETE FROM smf_sessions
		WHERE last_update < _int|
	DELETE FROM smf_log_search_results
				WHERE id_search = _int|
	DELETE FROM smf_log_search_topics
							WHERE id_search = _int|
	DELETE FROM smf_log_search_messages
							WHERE id_search = _int|
	DELETE FROM smf_log_online
				WHERE id_member = _int|
	DELETE FROM smf_log_online
			WHERE id_member = _int|
	DELETE FROM smf_topics
		WHERE id_topic IN (_array_int)|
	DELETE FROM smf_log_search_subjects
		WHERE id_topic IN (_array_int)|
	DELETE FROM smf_log_topics
			WHERE id_topic IN (_array_int)|
	DELETE FROM smf_log_topics
				WHERE id_topic IN (_array_int)|
	DELETE FROM smf_polls
			WHERE id_poll IN (_array_int)|
	DELETE FROM smf_poll_choices
			WHERE id_poll IN (_array_int)|
	DELETE FROM smf_log_polls
			WHERE id_poll IN (_array_int)|
	DELETE FROM smf_themes
		WHERE id_theme != _int
		AND variable = _string|
	DELETE FROM smf_log_mark_read
			WHERE id_board IN (_array_int)
				AND id_member = _int|
	DELETE FROM smf_log_boards
			WHERE id_board IN (_array_int)
				AND id_member = _int|
	DELETE FROM smf_log_topics
			WHERE id_member = _int
				AND id_topic IN (_array_int)|
	DELETE FROM smf_moderators
			WHERE id_board = _int|
	DELETE FROM smf_log_mark_read
		WHERE id_board IN (_array_int)|
	DELETE FROM smf_log_boards
		WHERE id_board IN (_array_int)|
	DELETE FROM smf_log_notify
		WHERE id_board IN (_array_int)|
	DELETE FROM smf_moderators
		WHERE id_board IN (_array_int)|
	DELETE FROM smf_calendar
		WHERE id_board IN (_array_int)|
	DELETE FROM smf_message_icons
		WHERE id_board IN (_array_int)|
	DELETE FROM smf_boards
		WHERE id_board IN (_array_int)|
	DELETE FROM smf_calendar
		WHERE id_event = _int|
	DELETE FROM smf_calendar_holidays
		WHERE id_holiday IN (_array_int)|
	DELETE FROM smf_collapsed_categories
		WHERE id_cat IN (_array_int)|
	DELETE FROM smf_categories
		WHERE id_cat IN (_array_int)|
	DELETE FROM smf_collapsed_categories
			WHERE id_cat IN (_array_int)|
	DELETE FROM smf_attachments
			WHERE id_attach = _int|
	DELETE FROM smf_membergroups
		WHERE id_group IN (_array_int)|
	DELETE FROM smf_permissions
		WHERE id_group IN (_array_int)|
	DELETE FROM smf_board_permissions
		WHERE id_group IN (_array_int)|
	DELETE FROM smf_group_moderators
		WHERE id_group IN (_array_int)|
	DELETE FROM smf_log_group_requests
		WHERE id_group IN (_array_int)|
	DELETE FROM smf_members
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_log_actions
		WHERE id_log = _int
			AND id_member IN (_array_int)|
	DELETE FROM smf_log_boards
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_log_comments
		WHERE id_recipient IN (_array_int)
			AND comment_type = _string|
	DELETE FROM smf_log_group_requests
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_log_karma
		WHERE id_target IN (_array_int)
			OR id_executor IN (_array_int)|
	DELETE FROM smf_log_mark_read
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_log_notify
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_log_online
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_log_subscribed
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_log_topics
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_collapsed_categories
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_pm_recipients
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_moderators
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_group_moderators
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_ban_items
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_themes
		WHERE id_member IN (_array_int)|
	DELETE FROM smf_openid_assoc
			WHERE expires <= _int|
	DELETE FROM smf_openid_assoc
		WHERE handle = _string|
	DELETE FROM smf_log_search_subjects
			WHERE id_topic = _int|
	DELETE FROM smf_log_online
				WHERE log_time < _int
					AND session != _string|
	DELETE FROM smf_log_floodcontrol
		WHERE log_time < _int
			AND log_type = _string|
	DELETE FROM smf_pm_recipients
			WHERE id_pm = _int|
	DELETE FROM smf_messages
				WHERE id_msg = _int|
	DELETE FROM smf_approval_queue
			WHERE id_msg IN (_array_int)
				AND id_attach = _int|
	DELETE FROM smf_themes
					WHERE id_theme != _int
						AND id_member = _int
						AND variable IN (_array_string)|
	DELETE FROM smf_themes
					WHERE id_theme = _int
						AND id_member != _int
						AND variable = SUBSTRING(_string, 1, 255)|
	DELETE FROM smf_themes
					WHERE variable = _string
						AND id_member > _int|
	DELETE FROM smf_themes
				WHERE id_theme != _int
					AND id_member > _int
					AND variable IN (_array_string)|
	DELETE FROM smf_themes
					WHERE id_theme = _int
						AND id_member != _int
						AND variable = SUBSTRING(_string, 1, 255)|
	DELETE FROM smf_themes
					WHERE variable = _string
						AND id_member > _int
						AND id_theme = _int|
	DELETE FROM smf_themes
			WHERE id_member > _int
				AND id_theme = _int
				|
	DELETE FROM smf_themes
		WHERE id_theme = _int|
	DELETE FROM smf_themes
					WHERE id_theme = _int
						AND variable = _string;

