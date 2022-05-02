pub fn new_private(counter: &mut u32, prefix: &str) -> String {
	let s = format!("{}{}", prefix, *counter);
	*counter += 1;
	s
}

/*
pub fn new_ident(ident: &str, prefix: &str) -> String {
	format!("{}_{}", prefix, ident)
}

bool lbl_equal_to_ident(const char *lbl, const char *ident, const char *prefix)
{
	size_t prefix_len = strlen(prefix);

	if(strncmp(lbl, prefix, prefix_len))
		return false;

	if(lbl[prefix_len] != '_')
		return false;

	if(strcmp(lbl + prefix_len + 1, ident))
		return false;

	return true;
}
*/
