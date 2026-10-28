"""Host-only controlled end of Home observation; never changes target sources."""
def controlled_home_source(text):
    marker='while(alive(&now)) {'
    assert text.count(marker)==1, 'Expected exactly one production Home main-loop clock condition'
    assert 'badge_fixture_home_continue' not in text
    return 'extern int badge_fixture_home_continue(void);\n'+text.replace(marker,'while(alive(&now) && badge_fixture_home_continue()) {')


def home_watchdog_define(flags):
    """Use the selected Home compile recipe, without calling a loaded app."""
    import re
    values=[int(m.group(1))*1000 for flag in flags if (m:=re.fullmatch(r'-DWATCH_DEFAULT_IDLE_SECONDS=(\d+)[uU]?',flag))]
    assert len(values)==1 and 0<values[0]<=0xffffffff, 'Expected one valid compiled Home idle duration'
    return '-DBADGE_FIXTURE_HOME_IDLE_MS='+str(values[0])+'ULL'
