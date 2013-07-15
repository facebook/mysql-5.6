<?php
// Copyright 2013-present Facebook.  All rights reserved.
// @author: rahulgulati (rahulgulati@fb.com)

final class FacebookMysqlLinter extends ArcanistLinter {

	const LINT_DOS_NEWLINE		= 1;
	const LINT_LINE_WRAP		= 2;
	const LINT_EOF_NEWLINE		= 3;
	const LINT_TRAILING_WHITESPACE	= 4;

	private $maxLineLength = 80;

	public function setMaxLineLength($new_length) {
		$this->maxLineLength = $new_length;
		return $this;
	}

	public function willLintPaths(array $paths) {
		return;
	}

	public function getLinterName() {
		return 'MYSQL';
	}

	public function getLintSeverityMap() {
		return array(
			self::LINT_DOS_NEWLINE
				=> ArcanistLintSeverity::SEVERITY_WARNING,
			self::LINT_LINE_WRAP
				=> ArcanistLintSeverity::SEVERITY_WARNING,
			self::LINT_EOF_NEWLINE
				=> ArcanistLintSeverity::SEVERITY_WARNING,
			self::LINT_TRAILING_WHITESPACE
				=> ArcanistLintSeverity::SEVERITY_AUTOFIX,
		);
	}

	public function getLintNameMap() {
		return array(
			self::LINT_DOS_NEWLINE
				=> 'DOS Newlines',
			self::LINT_LINE_WRAP
				=> 'Line Too Long',
			self::LINT_EOF_NEWLINE
				=> 'File Does Not End in Newline',
			self::LINT_TRAILING_WHITESPACE
				=> 'Trailing Whitespace',
		);
	}

	public function lintPath($path) {
		if ($this->didStopAllLinters()) {
			return;
		}

		if ($this->isBinaryFile($path)) {
			return;
		}

		if (!strlen($this->getData($path))) {
			/* If the file is empty, then we don't require to
			add a new line */
			return;
		}

		$this->lintNewLines($path);
		$this->lintLineLength($path);
		$this->lintEOFNewline($path);
		$this->lintTrailingWhitespace($path);
	}

	protected function lintNewlines($path) {
		$pos = strpos($this->getData($path), "\r");
		if ($pos !== false) {
			$this->raiseLintAtOffset(
				$pos,
				self::LINT_DOS_NEWLINE,
				'You must only use UNIX linebreaks ("\n") in source code',
				"\r");
		}
	}

	protected function lintLineLength($path) {
		$lines = explode("\n", $this->getData($path));
		$width = $this->maxLineLength;
		$tabLength = 8;
		foreach ($lines as $line_idx => $line) {
			$lineLength = strlen($line) + substr_count($line, "\t")
						      * ($tabLength - 1);
			if ($lineLength > $width) {
				$this->raiseLintAtLine(
					$line_idx + 1,
					1,
					self::LINT_LINE_WRAP,
					'This line is '.$lineLength.' characters long, '.
					'but the convention is '.$width.' characters.',
					$line);
			}
		}
	}

	protected function lintEOFNewline($path) {
		$data = $this->getData($path);
		if (!strlen($data) || $data[strlen($data) - 1] != "\n") {
			$this->raiseLintAtOffset(
				strlen($data),
				self::LINT_EOF_NEWLINE,
				"Files must end in a newline.",
				'',
				"\n");
		}
	}

	protected function lintTrailingWhitespace($path) {
		$data = $this->getData($path);

		$matches = null;
		$preg = preg_match_all(
			'/[ \t]+$/m',
			$data,
			$matches,
			PREG_OFFSET_CAPTURE);

		if (!$preg) {
			return;
		}

		foreach($matches[0] as $match) {
			list($string, $offset) = $match;
			$this->raiseLintAtOffset(
				$offset,
				self::LINT_TRAILING_WHITESPACE,
				'This line contains trailing whitespace.',
				$string,
				'');

		}
	}
}
