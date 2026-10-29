<?xml version="1.0" encoding="UTF-8"?>
<!--
  multitap-editor.xsl - the panel change of specs/iterum-tap-editor/design.md section 6, applied to the MultiTapPanel
  template that specs/iterum-ui-redesign/xslt/multitap.xsl built. Identity transform for everything else; every
  change is guarded, so a second pass changes nothing.

    npx xslt3 -xsl:specs/iterum-tap-editor/xslt/multitap-editor.xsl -s:plugins/iterum/resources/editor.uidesc -o:OUT

  Row 7:  the fieldset is titled TAP PATTERN (was CUSTOM PATTERN); it is always visible (no anchor controller).
  Row 7e: CTextLabel "Pattern Length" 330,4 92x16, tag 922, opaque, no frame (masks the fieldset's title line).
  Row 7f: COptionMenu 426,0 110x24 on MultiTapCustomLength (914), the 4.7 menu set (the Snap menu's attributes).
  Row 7a: the "Snap" label gains tag 922 and style-no-frame (P18).
  Row 8:  the Copy caption (tag 921) becomes opaque on the panel colour (finding 7), without a frame (an opaque
          label strokes one, as the Snap label did before P18).
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                xmlns:saxon="http://saxon.sf.net/"
                exclude-result-prefixes="xs saxon">

  <xsl:output method="xml" encoding="UTF-8" indent="yes" saxon:indent-spaces="3" saxon:line-length="100000"/>
  <xsl:strip-space elements="*"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:variable name="panel" select="/vstgui-ui-description/template[@name = 'MultiTapPanel']"/>

  <!-- 914 is a parameter (bound by name); 922 is a visibility tag, not a parameter (as 921) -->
  <xsl:template match="/vstgui-ui-description/control-tags">
    <xsl:copy>
      <xsl:apply-templates select="@* | node()"/>
      <xsl:if test="not(control-tag[@name = 'MultiTapCustomLength'])">
        <control-tag name="MultiTapCustomLength" tag="914"/>
      </xsl:if>
      <xsl:if test="not(control-tag[@name = 'MultiTapPatternEditorLabel'])">
        <control-tag name="MultiTapPatternEditorLabel" tag="922"/>
      </xsl:if>
    </xsl:copy>
  </xsl:template>

  <!-- Row 7: the fieldset that holds the editor -->
  <xsl:template match="template[@name = 'MultiTapPanel']/view[@class = 'FieldsetContainer']
                       [view[@custom-view-name = 'TapPatternEditor']]/@fieldset-title">
    <xsl:attribute name="fieldset-title" select="'TAP PATTERN'"/>
  </xsl:template>

  <!-- Rows 7e, 7f before the Snap label; row 7a on the Snap label -->
  <xsl:template match="template[@name = 'MultiTapPanel']/view[@class = 'FieldsetContainer']
                       [view[@custom-view-name = 'TapPatternEditor']]/view[@class = 'CTextLabel'][@title = 'Snap']">
    <xsl:if test="not(../view[@control-tag = 'MultiTapCustomLength'])">
      <view class="CTextLabel" origin="330, 4" size="92, 16" title="Pattern Length"
            control-tag="MultiTapPatternEditorLabel" font="label-font" font-color="text-dim" text-alignment="right"
            transparent="false" back-color="panel" style-no-frame="true"/>
      <xsl:variable name="menu" select="../view[@class = 'COptionMenu'][@control-tag = 'MultiTapSnapDivision']"/>
      <view class="COptionMenu" origin="426, 0" size="110, 24" control-tag="MultiTapCustomLength">
        <xsl:copy-of select="$menu/@* except ($menu/@class, $menu/@origin, $menu/@size, $menu/@control-tag)"/>
      </view>
    </xsl:if>
    <xsl:copy>
      <xsl:apply-templates select="@*"/>
      <xsl:if test="not(@control-tag)">
        <xsl:attribute name="control-tag" select="'MultiTapPatternEditorLabel'"/>
      </xsl:if>
      <xsl:attribute name="style-no-frame" select="'true'"/>
      <xsl:apply-templates select="node()"/>
    </xsl:copy>
  </xsl:template>

  <!-- Row 8: the Copy caption masks the fieldset's title line -->
  <xsl:template match="template[@name = 'MultiTapPanel']/view[@class = 'CViewContainer']
                       /view[@class = 'CTextLabel'][@control-tag = 'MultiTapCopyPatternButton']">
    <xsl:copy>
      <xsl:apply-templates select="@* except (@transparent, @back-color, @style-no-frame)"/>
      <xsl:attribute name="transparent" select="'false'"/>
      <xsl:attribute name="back-color" select="'panel'"/>
      <!-- an opaque CTextLabel strokes a frame unless told not to (the black box of P18) -->
      <xsl:attribute name="style-no-frame" select="'true'"/>
      <xsl:apply-templates select="node()"/>
    </xsl:copy>
  </xsl:template>

</xsl:stylesheet>
